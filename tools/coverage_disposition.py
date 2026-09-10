"""Explain intended treatment of a recorded inventory, without writing reviews.

Rules use authored roles and source record types. A flat segmentation label is
not evidence that a drawing should stay flat. Unknown roles stay explicit.
"""
import hashlib
import json
from pathlib import Path

DISPOSITIONS = ('unresolved', 'model', 'terrain', 'intentional_flat', 'animated_effect', 'runtime_state')
MILESTONES = tuple(f'M{i}' for i in range(12))


class Policy:
    def __init__(self, db):
        self.identity = dict(version=1, sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                             scope='Intended treatment of the recorded ledger snapshot; no review approval or runtime proof')
        self.models = {}
        for raw in db.execute("SELECT * FROM entries WHERE present=1 AND kind='model'"):
            r = dict(raw); d = json.loads(r['detail'])
            if (d.get('source_matches') is True and r['implementation'] in ('modeled', 'ground_only')
                    and r['category'] in ('model', 'ground_mask')):
                self.models[r['id'].removeprefix('model:')] = ('intentional_flat' if r['category']=='ground_mask' else 'model')

    def classify(self, row, review=None):
        r = dict(row)
        d = json.loads(r['detail']) if isinstance(r['detail'], str) else r['detail']
        source = d.get('source', {})
        location = source.get('path', '')
        if source.get('line'): location += ':' + str(source['line'])
        if source.get('pointer'): location += '#' + source['pointer']
        evidence = [location] if location else [r['id']]

        def decision(value, rule, milestone, reason, action, refs=None):
            return dict(disposition=value, basis='policy', rule=rule, milestone=milestone,
                        reason=reason, next_action=action, evidence=refs or evidence)

        kind, category = r['kind'], r['category']
        if kind in ('model', 'instance') and r['disposition'] in ('model', 'intentional_flat'):
            floor = r['disposition']=='intentional_flat'
            result = decision(r['disposition'], 'authored_role', 'M4',
                              'Authored source-only mask; it emits no solid.' if floor else 'Authored solid definition or its exact placement.',
                              'Review ownership and surrounding ground; terrain heights remain M2.' if floor else 'Review the model and every placement from all sides.')
        elif kind=='placement':
            models = d.get('models', [])
            roles = {self.models.get(id, 'unresolved') for id in models}
            complete = d.get('member_cells', 0)>0 and d.get('claimed_cells')==d['member_cells']
            if complete and models and len(roles)==1 and 'unresolved' not in roles and r['implementation'] in ('modeled', 'ground_only'):
                role = next(iter(roles))
                result = decision(role, 'complete_authored_coverage', 'M4',
                                  'Every source member cell has the same authored role; pixel ownership and appearance still need review.',
                                  'Check this occurrence and its ground boundary; matching does not approve it.', ['model:'+id for id in models])
            else:
                result = decision('unresolved', 'source_ownership_needed', 'M1',
                                  'Missing, partial or mixed authored roles; the segmentation label cannot decide the intended treatment.',
                                  'Inspect object, ground and shadow ownership, then assign M2 terrain or M4 scenery work.')
        elif kind=='family':
            result = decision('unresolved', 'family_context_needed', 'M1',
                              'A source family can have different treatment in different placements.',
                              'Inspect occurrence evidence before recording a family disposition; it will not approve placements.')
        elif kind=='export_gap':
            result = decision('unresolved', 'retained_export_gap', 'M2',
                              'Oversized source group and fragments remain an export gap; a mass label does not prove terrain.',
                              'Implement bounded chunks and inspect their source ownership.')
        elif kind=='map':
            result = decision('runtime_state', 'map_container', 'M5',
                              'Map identity and scene container; its drawings have separate entries.',
                              'Verify scene identity, transitions and compatible live capture.')
        elif kind=='state':
            result = self.state(r, d, decision)
        else:
            result = decision('unresolved', 'inventory_scope_needed', 'M1',
                              'This inventory scope or entry type still needs source investigation.',
                              'Split it into source-backed entries and record the remaining gaps.')

        if review:
            if review['invalidated']:
                result = decision('unresolved', 'stale_disposition', 'M1',
                                  'Earlier disposition needs reconsideration: ' + review['invalidated'],
                                  'Inspect changed inputs and record a fresh disposition.', [review['evidence']])
                result.update(basis='stale_review', previous_disposition=review['result'])
            else:
                value = review['result']
                if value not in DISPOSITIONS: raise ValueError('Unknown recorded disposition')
                owner = {'model':'M4', 'terrain':'M2', 'intentional_flat':'M4', 'unresolved':'M1'}.get(value)
                owner = owner or (result['milestone'] if result['milestone']!='M1' else 'M6' if value=='animated_effect' else 'M5')
                result = decision(value, 'recorded_disposition', owner, review['note'],
                                  'Follow the recorded intent and collect independent visual/live/headset evidence.', [review['evidence']])
                result['basis'] = 'review'
        return result

    @staticmethod
    def state(r, d, decide):
        category = r['category']
        path = d.get('source', {}).get('path', '')
        if d.get('source_status')=='unresolved_reference' or d.get('recognized') is False:
            return decide('unresolved', 'unresolved_source_reference', 'M1',
                          'The source adapter retained an unresolved or unknown reference.',
                          'Resolve its identity and dependencies before choosing its treatment.')
        if category in ('native_mutation_trace', 'native_state_reference', 'native_special_definition', 'script_native_call'):
            return decide('unresolved', 'native_audit_needed', 'M1',
                          'Native candidates are not a complete audit of effects, pointer paths or memory writes.',
                          'Inspect witnesses and unresolved references; verify the resulting runtime changes under M5.')
        if category in ('object_event', 'object_graphic', 'field_effect'):
            return decide('animated_effect', 'field_actor_or_effect', 'M6',
                          'Source object graphics/event or field-effect definition uses the actor/effect presentation pipeline.',
                          'Resolve variable identities, spawning, frames, timing and occlusion; authored exceptions can override this intent.')
        if category in ('sprite_definition', 'actor_animation', 'animation_table'):
            if path.startswith(('src/data/object_events/', 'src/data/field_effects/')):
                return decide('animated_effect', 'field_sprite_declaration', 'M6',
                              'Sprite/animation declaration in the inspected field graphics tables.',
                              'Validate rendered frames, poses, palette identity and gameplay timing.')
            if path.startswith(('src/battle/', 'src/battle_')):
                return decide('animated_effect', 'battle_sprite_declaration', 'M7',
                              'Sprite/animation declaration in the battle source, not a field actor.',
                              'Verify battle presentation, transitions and UI composition.')
            return decide('unresolved', 'sprite_domain_needed', 'M1',
                          'A sprite declaration alone does not identify field, menu, battle or other presentation ownership.',
                          'Trace its source consumer before assigning M6 or M7 work.')
        if category in ('tileset_animation_callback', 'tileset_animation_frame'):
            if category=='tileset_animation_callback' and d.get('no_callback') is True:
                return decide('runtime_state', 'explicit_null_tileset_callback', 'M4',
                              'The source explicitly declares no callback for this tileset; this is configuration, not a drawable.',
                              'Retain the null declaration; other routes may still change its tiles.')
            return decide('unresolved', 'tileset_animation_ownership_needed', 'M1',
                          'Animation data may affect terrain, scenery or both; callback/frame presence does not identify affected texels.',
                          'Trace the frame updates and bind them to source roles before implementing playback.')
        if category in ('weather', 'coordinate_weather', 'script_weather_time'):
            return decide('runtime_state', 'weather_control', 'M8',
                          'Source weather/time control; it is not a separate static model.',
                          'Verify authoritative weather/time and presentation without changing guest clock or save state.')
        if category=='metatile_behavior':
            return decide('runtime_state', 'gameplay_surface_constraint', 'M2',
                          'Gameplay behavior/collision metadata; its numeric value is not a physical height.',
                          'Derive surface and placement constraints from actual gameplay semantics.')
        if category in ('movement_command', 'script_field_effect'):
            return decide('runtime_state', 'actor_effect_control', 'M6',
                          'Source command controls an actor/effect; the graphic has its own inventory entry.',
                          'Verify execution, timing, offsets and the selected actor/effect.')
        if category=='script_presentation':
            return decide('runtime_state', 'presentation_control', 'M7',
                          'Script presentation command; affected screens/layers need separate runtime coverage.',
                          'Verify dialogue, menu, fade, audio and battle routing for the actual command.')
        if category in ('connection', 'warp', 'script_warp', 'script_tile_change', 'script_actor_state',
                        'script_block', 'background_event', 'coordinate_event', 'map_environment', 'map_type'):
            return decide('runtime_state', 'scene_state_control', 'M5',
                          'Source scene/event state rather than a standalone drawing; conditions and runtime values remain unverified.',
                          'Capture and verify transitions, conditional state and same-map updates; preserve saved-destination cases.')
        return decide('unresolved', 'unknown_source_category', 'M1',
                      'No inspected treatment rule covers this source category.',
                      'Inspect the source and add a bounded rule or recorded disposition.')
