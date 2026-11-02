/*
 * Empty stand-in for libskia.so. About eight MTK camera/imaging blobs list it
 * as DT_NEEDED but the camera chain (libcam.camadapter et al.) imports no Skia
 * symbol from it. Pie has no system libskia.so (Skia is linked into libhwui),
 * so the dependency just has to be satisfiable. Installed as libskia.so from
 * module libskia_stub.
 */
__attribute__((visibility("default")))
int mtk_libskia_stub;
