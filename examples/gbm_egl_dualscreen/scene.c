#include "scene.h"
#include <GLES2/gl2.h>
#include <stdio.h>

static GLuint prog;
static GLint loc_pos, loc_res, loc_time, loc_panel;

static const char *vs =
    "attribute vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

/* A slow plasma tinted per panel (warm on top, cool on the bottom) with a bright disc whose path
 * is mirrored between the two panels, so motion is visibly continuous across the hinge. */
static const char *fs =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform vec2 u_res;\n"
    "uniform float u_time;\n"
    "uniform float u_panel;\n"
    "void main() {\n"
    "  vec2 uv = gl_FragCoord.xy / u_res;\n"
    "  float v = sin(uv.x * 9.0 + u_time) + sin(uv.y * 7.0 - u_time * 1.3)\n"
    "          + sin((uv.x + uv.y) * 6.0 + u_time * 0.7);\n"
    "  vec3 warm = vec3(0.85, 0.35, 0.20), cool = vec3(0.20, 0.45, 0.90);\n"
    "  vec3 col = mix(warm, cool, u_panel) * (0.55 + 0.25 * v);\n"
    "  /* one vertical coordinate for the stacked pair, y up: bottom panel 0..1, top panel 1..2 */\n"
    "  float g = u_panel < 0.5 ? 1.0 + uv.y : uv.y;\n"
    "  vec2 c = vec2(0.5 + 0.35 * sin(u_time * 0.8), 1.0 + 0.85 * sin(u_time * 0.5));\n"
    "  float d = length(vec2((uv.x - c.x) * u_res.x / u_res.y, g - c.y));\n"
    "  col += vec3(1.0) * smoothstep(0.16, 0.12, d);\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "shader compile: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

int scene_init(void)
{
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f)
        return -1;
    prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glBindAttribLocation(prog, 0, "a_pos");
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        fprintf(stderr, "program link: %s\n", log);
        return -1;
    }
    loc_pos = 0;
    loc_res = glGetUniformLocation(prog, "u_res");
    loc_time = glGetUniformLocation(prog, "u_time");
    loc_panel = glGetUniformLocation(prog, "u_panel");
    return 0;
}

void scene_draw(int panel, int width, int height, float t)
{
    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glViewport(0, 0, width, height);
    glUseProgram(prog);
    glUniform2f(loc_res, (float)width, (float)height);
    glUniform1f(loc_time, t);
    glUniform1f(loc_panel, (float)panel);
    glEnableVertexAttribArray(loc_pos);
    glVertexAttribPointer(loc_pos, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void scene_fini(void)
{
    if (prog)
        glDeleteProgram(prog);
    prog = 0;
}
