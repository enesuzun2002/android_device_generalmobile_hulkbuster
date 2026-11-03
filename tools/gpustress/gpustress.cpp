/*
 * gpustress: render a fixed, ALU-heavy fragment shader into an offscreen
 * pbuffer, read every frame back and compare its checksum with the first
 * frame. Any difference is silent GPU corruption (clock/voltage too
 * aggressive). Usage: gpustress <seconds> [size]
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vector>

static const char* kVs =
    "attribute vec2 p; varying vec2 uv;"
    "void main(){ uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }";
static const char* kFs =
    "precision mediump float; varying vec2 uv;"
    "void main(){ vec3 c = vec3(uv, 0.5);"
    "  for (int i = 0; i < 48; i++) {"
    "    c = fract(c * 1.37 + vec3(sin(c.y * 6.0), cos(c.z * 5.0), sin(c.x * 4.0)) * 0.25);"
    "  }"
    "  gl_FragColor = vec4(c, 1.0); }";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "shader: %s\n", log);
        exit(2);
    }
    return s;
}

static double now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char** argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 60;
    int size = argc > 2 ? atoi(argv[2]) : 512;

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(d, nullptr, nullptr);
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                              EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(d, cfgAttr, &cfg, 1, &n) || n < 1) { fprintf(stderr, "no config\n"); return 2; }
    const EGLint pbAttr[] = {EGL_WIDTH, size, EGL_HEIGHT, size, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(d, cfg, pbAttr);
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT || !eglMakeCurrent(d, surf, surf, ctx)) {
        fprintf(stderr, "egl setup failed 0x%x\n", eglGetError());
        return 2;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, kVs));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, kFs));
    glBindAttribLocation(prog, 0, "p");
    glLinkProgram(prog);
    glUseProgram(prog);
    const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glViewport(0, 0, size, size);

    std::vector<uint8_t> px(size * size * 4);
    uint64_t ref = 0;
    long frames = 0, bad = 0;
    double end = now() + secs, t0 = now();
    while (now() < end) {
        // Several overdraw passes per readback keep the GPU busy.
        for (int k = 0; k < 4; k++) glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        uint64_t h = 1469598103934665603ULL;  // FNV-1a
        for (uint8_t b : px) { h ^= b; h *= 1099511628211ULL; }
        if (frames == 0) ref = h; else if (h != ref) bad++;
        if (glGetError() != GL_NO_ERROR) { fprintf(stderr, "gl error\n"); bad++; }
        frames++;
    }
    double el = now() - t0;
    printf("frames=%ld fps=%.1f mismatches=%ld ref=%016llx %s\n", frames, frames / el, bad,
           (unsigned long long)ref, bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
