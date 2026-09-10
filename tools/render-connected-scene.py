"""Compatibility entry point for the current connected-area acceptance GIF.

Run tools/test-region-model-reuse.py first. To reproduce PR #30's historical
four-map GIF, use this script from that PR's merged revision instead.
"""
from pathlib import Path
import runpy

if __name__=='__main__':
    runpy.run_path(str(Path(__file__).with_name('render-region-model-reuse.py')),run_name='__main__')
