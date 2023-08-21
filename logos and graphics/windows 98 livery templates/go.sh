#! /usr/bin/env bash
set -e

rm -f passenger-tiles/*.png
rm -f driver-tiles/*.png
convert -crop 137x171 livery-passenger-flat.png passenger-tiles/tile%03d.png
convert -crop 137x171 livery-driver-flat.png driver-tiles/tile%03d.png
