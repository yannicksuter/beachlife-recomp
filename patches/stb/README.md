# stb patch

The pinned stb revision remains unchanged. Prepared builds apply the patch in
`series`; the dependency checkout is not edited.

The JPEG writer may be compiled with `STBIW_JPEG_SUBSAMPLING=1` to select
Y 2x2 / Cb,Cr 1x1 sampling at every quality, or `0` for all 1x1. Without the
macro, its existing quality-dependent behavior remains unchanged. The native
capture encoder opts into 2x2 sampling at quality100. Quantization and the
existing codec implementation remain unchanged.
