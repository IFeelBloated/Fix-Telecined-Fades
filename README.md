# Fix Telecined Fades
©2017 IFeelBloated, Fix Telecined Fades for VapourSynth

## License
GPL v3.0

## Description
The filter gives a mathematically perfect solution to such (fades were done AFTER telecine which made a picture perfect IVTC pretty much impossible) problem.
Unlike vinverse which works as a dumb blurring + contra-sharpening combo and very harmful to artifacts-free frames, this filter works by matching the brightness of top and bottom fields with statistical methods, and also harmless to healthy frames.

## Usage
```python
clip = core.ftf.FixFades(clip, mode=0, threshold=0.002, color=[0.0, 0.0, 0.0], opt=True)
```

## Options
* clip: Clip to be processed.

* mode: could be `0` (default), `1`, or `2`.
  * 0: Adjust the brightness of both fields to match the average brightness of 2 fields.
  * 1: Darken the brighter field to match the brightness of the darker field.
  * 2: Brighten the darker field to match the brightness of the brighter field.

* threshold: Threshold for the average difference per pixel, on a scale of `0.0` - `1.0`, but could go beyond `1.0`, the frame will remain untouched if the average difference between 2 fields goes below this value.

* color: Base color of the fade, default is `[0.0, 0.0, 0.0]`(black).

* opt: Call the fastest possible functions if `opt=True`, else call the C++ functions.

## Building from sources
You need [The Meson Build System](http://mesonbuild.com/) installed.
```
$ cd /path/to/src/root && mkdir build && cd build && meson --buildtype release .. && ninja
# ninja install
```
