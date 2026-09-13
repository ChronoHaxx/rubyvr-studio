# Public runner integration check

**2026-09-13, M5 / RV-007:** the current public framework provides useful menu
and native-function extension points. It does not contain the private adapter
hooks used by this demo. This is a source inspection, not a successful clean
build, a distribution permission, or a reason to replace the engine.

Inspected `mstan/gbarecomp` at
`3d61d5572e440fd0907b1529b85686d59a9cd314`, retrieved from GitHub's contents API:

| Interface | Finding |
|---|---|
| [RunOptions](https://github.com/mstan/gbarecomp/blob/3d61d5572e440fd0907b1529b85686d59a9cd314/src/runtime/runtime.h) | Existing setting/action/text callbacks can carry developer controls. Retain this upstream boundary. |
| [Native function hooks](https://github.com/mstan/gbarecomp/blob/3d61d5572e440fd0907b1529b85686d59a9cd314/src/runtime/mod_function_hooks.h) | Function-entry registration exists; it supports the bounded collision override. This does not supply a whole rendering adapter. |
| [HostWindow](https://github.com/mstan/gbarecomp/blob/3d61d5572e440fd0907b1529b85686d59a9cd314/src/runtime/host_window.h) and [event implementation](https://github.com/mstan/gbarecomp/blob/3d61d5572e440fd0907b1529b85686d59a9cd314/src/runtime/host_window.cpp) | No `set_frame_sink` equivalent to our current private hook; no RubyVR viewer-event dispatch. Both are required by the present integration. |
| [Runtime loop](https://github.com/mstan/gbarecomp/blob/3d61d5572e440fd0907b1529b85686d59a9cd314/src/runtime/runtime.cpp) | Present-in-place and load-epoch machinery exist, but our camera input filter, named checkpoint service and developer pacing/stepping calls are absent. They need a supported integration boundary. |

The local private framework passes all six marker checks; this pinned public
snapshot passes the settings callback check and lacks the other five current
hook groups. Marker presence does not prove correct timing or safe state loads.

Use this read-only check before attempting an unfamiliar checkout:

```text
python tools/check-runtime-host.py --framework /path/to/gbarecomp
```

It needs no game assets, SDL or OpenGL. Exit 1 means required source markers are
missing; exit 0 means those markers were found. It does not modify the checkout
or download dependencies. `--json` prints a report suitable for a setup guide.

For batch 3, the smallest useful path is to formalize and build the missing
host callbacks around the existing upstream interfaces, then prove a fresh
directory launch with the player's own inputs and save/load lifecycle. The
[existing combined-runtime distribution question](../integration/README.md#licence-compatibility-before-distribution)
also remains unresolved. No release, upstream post, new licence or copying of
restricted runtime implementation was done by this check.
