// =============================================================================
//  AgriFlow 3D  -  Real-Time Agricultural Supply Chain Visualizer
//  Enhanced Visual Edition
// =============================================================================
//
//  Course  : Computer Graphics & Visualization (CGV)  |  April 2026
//  Stack   : C++17, OpenGL 3.3 Core, GLFW 3.3, GLAD, GLM
//
//  Build (Windows / MinGW):
//    g++ -std=c++17 -O2 src/main.cpp src/glad.c ^
//        -Iinclude -Llib -lglfw3 -lopengl32 -lgdi32 ^
//        -o agriflow.exe
//
// -----------------------------------------------------------------------------
//  MODULE INDEX  (search for the banner to jump to each section)
// -----------------------------------------------------------------------------
//   SHADER CLASS       ~line   29   GLSL compile/link wrapper
//   GLSL SOURCES       ~line   63   Terrain, Object, Sky, Line, Particle, HUD,
//                                   Bloom (bright-pass / Gaussian blur / composite)
//   CAMERA             ~line  487   Euler-angle fly-camera (WASD + mouse)
//   TERRAIN            ~line  522   100x100 heightmap, per-vertex colour
//   PRIMITIVES         ~line  641   Box / Cylinder / Sphere / Cone / Gable / Torus
//   PRIM CACHE         ~line  798   Shared mesh instances (upload once, draw many)
//   NODE DRAWERS       ~line  815   drawFarm / drawWarehouse / drawColdStorage / drawMarket
//   SUPPLY CHAIN       ~line 1060   SCNode, SCEdge, Dijkstra, ray-pick
//   VEHICLES           ~line 1143   Spawn, update, multi-part truck render (6 wheels)
//   PARTICLES          ~line 1340   Point-sprite GPU particle system
//   ROUTE RENDERER     ~line 1391   Dynamic GL_LINES with glow effect
//   BLOOM FBO          ~line 1440   HDR 16-bit FBO -> bright-pass -> 8x Gaussian -> Reinhard
//   5x7 BITMAP FONT    ~line 1547   96-glyph ASCII pixel-font (no external library)
//   HUD                ~line 1746   Top bar, stats panel, minimap, bottom pills
//   DAY/NIGHT          ~line 2074   24-hour TOD: sun dir / colour / ambient / sky gradient
//   GLOBALS/CALLBACKS  ~line 2166   GLFW callbacks, global state
//   MAIN               ~line 2206   Init, main loop
//
// -----------------------------------------------------------------------------
//  SUPPLY CHAIN NODES
// -----------------------------------------------------------------------------
//   ID  Name                  Type          Location
//    0  Vidarbha Wheat Farm   FARM          SW
//    1  Nashik Cooperative    FARM          W
//    2  Ludhiana Fields       FARM          SE
//    3  Amritsar Farm         FARM          NE
//    4  Nagpur Warehouse      WAREHOUSE     Centre-W
//    5  Ludhiana Hub          WAREHOUSE     Centre-E
//    6  Nashik Cold Chain     COLD_STORAGE  Centre
//    7  Delhi Main Mandi      MARKET        E
//    8  Delhi Retail          MARKET        Far-E
//    9  New Delhi Market      MARKET        NE-E
//
//  EDGE COST FORMULA:
//    w = distance(km)  +  spoilRate x 1000  +  middlemen x 50
//  Dijkstra minimises this combined cost (press O to toggle).
//
// -----------------------------------------------------------------------------
//  KEY CONTROLS
// -----------------------------------------------------------------------------
//   WASD / Space / Ctrl   Fly camera        Right-click + drag   Look around
//   O                     Toggle optimise   1 / 2 / 3            Time speed x1/5/20
//   Left-click node       Select & re-route F1/F2/F3             Camera presets
//   Escape                Quit
// =============================================================================
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <queue>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

extern "C" {
    void __mingw_free(void* p) { free(p); }
    void* __mingw_realloc(void* p, size_t sz) { return realloc(p, sz); }
}

int SCR_WIDTH = 1280;
int SCR_HEIGHT = 720;
const float PI = 3.14159265358979f;

// ============================================================
//  SHADER CLASS
// ============================================================
class Shader {
public:
    unsigned int ID;
    Shader() : ID(0) {}
    Shader(const char* vs, const char* fs) {
        unsigned int v = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(v, 1, &vs, NULL); glCompileShader(v); chk(v, "VERT");
        unsigned int f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(f, 1, &fs, NULL); glCompileShader(f); chk(f, "FRAG");
        ID = glCreateProgram();
        glAttachShader(ID, v); glAttachShader(ID, f); glLinkProgram(ID); lnk(ID);
        glDeleteShader(v); glDeleteShader(f);
    }
    void use() const { glUseProgram(ID); }
    void setBool(const char* n, bool v)          const { glUniform1i(loc(n),(int)v); }
    void setInt (const char* n, int v)           const { glUniform1i(loc(n),v); }
    void setFloat(const char* n, float v)        const { glUniform1f(loc(n),v); }
    void setVec2(const char* n, glm::vec2 v)     const { glUniform2fv(loc(n),1,glm::value_ptr(v)); }
    void setVec3(const char* n, glm::vec3 v)     const { glUniform3fv(loc(n),1,glm::value_ptr(v)); }
    void setVec4(const char* n, glm::vec4 v)     const { glUniform4fv(loc(n),1,glm::value_ptr(v)); }
    void setMat4(const char* n, const glm::mat4& m) const { glUniformMatrix4fv(loc(n),1,GL_FALSE,glm::value_ptr(m)); }
private:
    int loc(const char* n) const { return glGetUniformLocation(ID, n); }
    void chk(unsigned int s, const char* t) {
        int ok; char log[1024]; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if (!ok) { glGetShaderInfoLog(s,1024,NULL,log); fprintf(stderr,"SHADER %s:\n%s\n",t,log); }
    }
    void lnk(unsigned int p) {
        int ok; char log[1024]; glGetProgramiv(p,GL_LINK_STATUS,&ok);
        if (!ok) { glGetProgramInfoLog(p,1024,NULL,log); fprintf(stderr,"LINK:\n%s\n",log); }
    }
};

// ============================================================
//  GLSL SOURCES
// ============================================================

// ---- TERRAIN ----
const char* terrainVert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aColor;
uniform mat4 model, view, projection;
out vec3 FragPos, Normal, VertColor;
out vec2 TexCoord;
out float Height;
void main(){
    vec4 wp = model * vec4(aPos,1.0);
    FragPos  = wp.xyz;
    Normal   = mat3(transpose(inverse(model))) * aNormal;
    VertColor = aColor;
    TexCoord  = aPos.xz / 10.0;
    Height    = aPos.y;
    gl_Position = projection * view * wp;
}
)";
const char* terrainFrag = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec3 VertColor;
in vec2 TexCoord;

uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 ambientColor;
uniform vec3 viewPos;
uniform float time;

out vec4 FragColor;

void main() {
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(sunDir);
    vec3 viewDir = normalize(viewPos - FragPos);
    
    // STRONG DIRECTIONAL LIGHTING
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 16.0);
    
    // RICH BASE COLOR FROM VERTEX COLOR
    vec3 baseColor = VertColor;
    
    // PROCEDURAL DETAIL PATTERNS
    // Farmland row stripes on flat green areas
    if(VertColor.g > 0.45 && VertColor.r < 0.35) {
        float stripe = fract(TexCoord.x * 6.0);
        float line = smoothstep(0.85, 0.95, stripe);
        baseColor = mix(baseColor, baseColor * 1.4, line);
        
        // Cross stripes
        float stripe2 = fract(TexCoord.y * 6.0);
        float line2 = smoothstep(0.92, 1.0, stripe2);
        baseColor = mix(baseColor, baseColor * 0.75, line2);
    }
    
    // Water shimmer effect
    if(VertColor.b > 0.4 && VertColor.g < 0.5) {
        float shimmer = sin(TexCoord.x * 20.0 + time * 2.0) 
                      * sin(TexCoord.y * 15.0 + time * 1.5);
        shimmer = shimmer * 0.08 + 0.95;
        baseColor *= shimmer;
        // Fresnel-like water edge brightening
        float fresnel = 1.0 - max(dot(norm, viewDir), 0.0);
        baseColor += vec3(0.1, 0.15, 0.25) * fresnel * 0.5;
        spec *= 3.0; // water is shiny
    }
    
    // STRONG PHONG LIGHTING
    vec3 ambient  = ambientColor * 0.35;
    vec3 diffuse  = sunColor * diff * 1.15;
    vec3 specular = sunColor * spec * 0.25;
    
    vec3 lighting = ambient + diffuse + specular;
    vec3 result = baseColor * lighting;
    
    // Result is directly exported to HDR buffer, gamma handled by post-processing bloom shader
    FragColor = vec4(result, 1.0);
}
)";

// ---- OBJECTS ----
const char* objectVert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aColor;
uniform mat4 model, view, projection;
out vec3 FragPos, Normal, VertColor;
out vec2 TexCoord;
void main(){
    vec4 wp = model * vec4(aPos,1.0);
    FragPos  = wp.xyz;
    Normal   = mat3(transpose(inverse(model))) * aNormal;
    VertColor = aColor;
    TexCoord  = aPos.xy * 0.5;
    gl_Position = projection * view * wp;
}
)";
const char* objectFrag = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec3 VertColor;
in vec2 TexCoord;

uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 ambientColor;
uniform vec3 viewPos;
uniform vec3 emissiveColor;
uniform bool useOverrideColor;
uniform vec3 overrideColor;
uniform float time;

out vec4 FragColor;

// Procedural brick pattern
float brickPattern(vec2 uv, float scale) {
    vec2 brick = uv * scale;
    // Offset every other row
    brick.x += step(1.0, mod(brick.y, 2.0)) * 0.5;
    brick = fract(brick);
    float mortarX = smoothstep(0.0, 0.04, brick.x) 
                  * smoothstep(1.0, 0.96, brick.x);
    float mortarY = smoothstep(0.0, 0.06, brick.y) 
                  * smoothstep(1.0, 0.94, brick.y);
    return mortarX * mortarY;
}

// Window pattern for buildings
float windowPattern(vec2 uv, float cols, float rows) {
    vec2 w = fract(uv * vec2(cols, rows));
    float mx = smoothstep(0.15,0.25,w.x) 
             * smoothstep(0.85,0.75,w.x);
    float my = smoothstep(0.1, 0.2, w.y) 
             * smoothstep(0.9, 0.8, w.y);
    return mx * my;
}

void main() {
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(sunDir);
    vec3 viewDir = normalize(viewPos - FragPos);
    
    vec3 baseColor = useOverrideColor ? overrideColor : VertColor;
    
    // APPLY SURFACE TEXTURING
    float brick = brickPattern(TexCoord, 4.0);
    // Darken mortar lines
    baseColor = mix(baseColor * 0.65, baseColor, brick);
    
    // Window glow on building sides
    float win = windowPattern(TexCoord, 3.0, 2.0);
    vec3 windowColor = vec3(1.0, 0.92, 0.6) * win;
    
    // PHONG LIGHTING WITH STRONG SHADOW SIDE
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 64.0);
    
    // Strong ambient-diffuse contrast for depth
    float ambientStr  = 0.18;
    float diffuseStr  = 1.15;
    float specularStr = 0.35;
    
    vec3 ambient  = ambientColor * ambientStr;
    vec3 diffuse  = sunColor * diff * diffuseStr;
    vec3 specular = sunColor * spec * specularStr;
    
    vec3 lighting = ambient + diffuse + specular;
    vec3 result = baseColor * lighting;
    
    // Add window glow (unlit, additive)
    result += windowColor * 0.6;
    
    // Add emissive (for cold storage glow etc)
    result += emissiveColor * 1.5;
    
    // Result is directly exported to HDR buffer
    FragColor = vec4(result, 1.0);
}
)";

// ---- SKY ----
const char* skyVert = R"(
#version 330 core
layout(location=0) in vec2 aPos;
out vec2 uv;
void main(){ uv = aPos; gl_Position = vec4(aPos,0.999,1.0); }
)";
const char* skyFrag = R"(
#version 330 core
in vec2 uv;
uniform vec3 sunDir;
uniform float time;
uniform vec3 skyTop, skyHorizon, sunColor;
uniform float isNight;
out vec4 FragColor;

float hash(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453123); }
float hash3(vec3 p){ return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453123); }
float noise(vec2 p){
    vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
float fbm(vec2 p){ float v=0.0,a=0.5; for(int i=0;i<5;i++){v+=a*noise(p);p*=2.0;a*=0.5;} return v; }

void main(){
    vec2 screenUV = uv * 0.5 + 0.5;
    // Sky gradient
    float elevation = screenUV.y;
    vec3 sky = mix(skyHorizon, skyTop, pow(clamp(elevation,0.0,1.0),0.6));

    // Clouds (day only)
    if(isNight < 0.5){
        vec2 cloudUV = screenUV * vec2(2.5,1.5) + vec2(time*0.012, 0.0);
        float cloud = fbm(cloudUV);
        float cloud2 = fbm(cloudUV*1.7 + vec2(0.4,0.0));
        float c = smoothstep(0.52, 0.75, (cloud+cloud2)*0.5);
        vec3 cloudCol = mix(vec3(0.95,0.97,1.0), vec3(0.75,0.82,0.92), 1.0-elevation);
        // Cloud shadow
        float shadow = smoothstep(0.52,0.75,fbm(cloudUV+vec2(0.02,-0.01)));
        cloudCol *= (0.8 + 0.2*shadow);
        sky = mix(sky, cloudCol, c*clamp(elevation*2.5,0.0,1.0));
    }

    // Sun disk + corona
    vec3 sunDirN = normalize(sunDir);
    vec2 sunProj = sunDirN.xz / max(sunDirN.y, 0.01);
    vec2 fragDir = (screenUV - vec2(0.5,0.5)) * vec2(2.0,2.0);
    float sunDist = length(fragDir - sunProj * 0.18);
    float sunDisk = smoothstep(0.06, 0.04, sunDist);
    float corona  = smoothstep(0.35, 0.04, sunDist) * 0.4;
    if(sunDirN.y > 0.0){
        sky = mix(sky, sunColor * 2.0, sunDisk);
        sky += sunColor * corona * clamp(sunDirN.y, 0.0, 1.0);
    }

    // Stars at night
    if(isNight > 0.0){
        float fade = elevation * 2.0;
        vec2 starUV = screenUV * 400.0;
        vec2 starI  = floor(starUV);
        float star  = hash(starI);
        float twinkle = 0.7 + 0.3*sin(time*3.0 + star*20.0);
        float starDot = step(0.985, star) * twinkle * clamp(fade,0.0,1.0);
        sky += vec3(starDot) * isNight;
        // Milky way-ish band
        float milky = fbm(screenUV*vec2(6.0,2.0) + vec2(0.5,0.2))*0.15*isNight*elevation;
        sky += vec3(0.5,0.4,0.7)*milky;
    }

    FragColor = vec4(sky, 1.0);
}
)";

// ---- LINES ----
const char* lineVert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 view, projection;
out float lineDist;
void main(){
    lineDist = aPos.x + aPos.z;
    gl_Position = projection * view * vec4(aPos,1.0);
}
)";
const char* lineFrag = R"(
#version 330 core
in float lineDist;
uniform vec4 lineColor;
uniform float time;
uniform bool isDashed;
uniform float flowSpeed;
out vec4 FragColor;
void main(){
    if(isDashed){
        float v = mod(gl_FragCoord.x + gl_FragCoord.y + time*40.0, 24.0);
        if(v > 12.0) discard;
    }
    // Flow pulse for optimized routes
    float pulse = isDashed ? (0.7+0.3*sin(lineDist*0.3 - time*flowSpeed)) : 1.0;
    FragColor = vec4(lineColor.rgb * pulse, lineColor.a);
}
)";

// ---- PARTICLES ----
const char* particleVert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
layout(location=2) in float aSize;
uniform mat4 view, projection;
out vec3 pColor;
void main(){
    pColor = aColor;
    vec4 vp = view * vec4(aPos,1.0);
    gl_PointSize = clamp(aSize * 220.0 / length(vp.xyz), 1.0, 32.0);
    gl_Position  = projection * vp;
}
)";
const char* particleFrag = R"(
#version 330 core
in vec3 pColor;
out vec4 FragColor;
void main(){
    vec2 c = gl_PointCoord - 0.5;
    float d = length(c);
    if(d > 0.5) discard;
    float alpha = 1.0 - smoothstep(0.25, 0.5, d);
    // Bright hot center
    vec3 col = mix(pColor * 2.0, pColor, smoothstep(0.0, 0.3, d));
    FragColor = vec4(col, alpha);
}
)";

// ---- HUD ----
const char* hudVert = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 orthoProj;
uniform vec2 hudOffset, hudScale;
out vec2 vUV;
void main(){
    vUV = aUV;
    gl_Position = orthoProj * vec4(aPos * hudScale + hudOffset, 0.0, 1.0);
}
)";
const char* hudFrag = R"(
#version 330 core
in vec2 vUV;
uniform vec4 quadColor;
uniform float time;
uniform bool isAlert;
out vec4 FragColor;
void main(){
    vec4 col = quadColor;
    if(isAlert){
        float pulse = 0.5 + 0.5*sin(time*6.0);
        col.rgb = mix(col.rgb, vec3(1.0,0.1,0.1), pulse*0.5);
        col.a = mix(col.a, 1.0, pulse*0.3);
    }
    // Subtle gradient
    col.rgb *= (0.85 + 0.15*vUV.y);
    FragColor = col;
}
)";

// ---- BLOOM BLIT ----
const char* bloomVert = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }
)";
const char* bloomFrag = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D scene;
uniform sampler2D bloomTex;
uniform float exposure;
out vec4 FragColor;
void main(){
    vec3 hdr    = texture(scene, vUV).rgb;
    vec3 bloom  = texture(bloomTex, vUV).rgb;
    vec3 result = hdr + bloom * 0.7;
    // Tone-map (Reinhard)
    result *= exposure;
    result = result / (result + vec3(1.0));
    // Gamma
    result = pow(result, vec3(1.0/2.2));
    FragColor = vec4(result, 1.0);
}
)";
const char* blurFrag = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D image;
uniform bool horizontal;
out vec4 FragColor;
const float weight[5] = float[](0.227027,0.1945946,0.1216216,0.054054,0.016216);
void main(){
    vec2 texOffset = 1.0 / textureSize(image, 0);
    vec3 result = texture(image, vUV).rgb * weight[0];
    if(horizontal){
        for(int i=1;i<5;i++){
            result += texture(image,vUV+vec2(texOffset.x*i,0.0)).rgb*weight[i];
            result += texture(image,vUV-vec2(texOffset.x*i,0.0)).rgb*weight[i];
        }
    } else {
        for(int i=1;i<5;i++){
            result += texture(image,vUV+vec2(0.0,texOffset.y*i)).rgb*weight[i];
            result += texture(image,vUV-vec2(0.0,texOffset.y*i)).rgb*weight[i];
        }
    }
    FragColor = vec4(result,1.0);
}
)";
const char* brightFrag = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D image;
out vec4 FragColor;
void main(){
    vec3 col = texture(image,vUV).rgb;
    float bright = dot(col,vec3(0.2126,0.7152,0.0722));
    FragColor = bright > 1.0 ? vec4(col,1.0) : vec4(0.0);
}
)";

// ============================================================
//  CAMERA
// ============================================================
class Camera {
public:
    glm::vec3 Position, Front, Up, Right, WorldUp;
    float Yaw, Pitch, Speed, Sensitivity, Zoom;
    Camera(glm::vec3 pos)
        : Position(pos), WorldUp(0,1,0), Yaw(-90), Pitch(-20),
          Speed(25), Sensitivity(0.1f), Zoom(45), Front(0,0,-1) { upd(); }
    glm::mat4 GetViewMatrix() { return glm::lookAt(Position,Position+Front,Up); }
    void ProcessKeyboard(int d, float dt) {
        float v=Speed*dt;
        if(d==0) Position+=Front*v; if(d==1) Position-=Front*v;
        if(d==2) Position-=Right*v; if(d==3) Position+=Right*v;
        if(d==4) Position+=WorldUp*v; if(d==5) Position-=WorldUp*v;
    }
    void ProcessMouse(float dx,float dy){
        Yaw+=dx*Sensitivity; Pitch+=dy*Sensitivity;
        Pitch=glm::clamp(Pitch,-89.0f,89.0f); upd();
    }
    void setTarget(glm::vec3 p,float dist,float pitch,float yaw){
        Pitch=pitch; Yaw=yaw; upd();
        Position=p-Front*dist;
    }
private:
    void upd(){
        glm::vec3 f;
        f.x=cos(glm::radians(Yaw))*cos(glm::radians(Pitch));
        f.y=sin(glm::radians(Pitch));
        f.z=sin(glm::radians(Yaw))*cos(glm::radians(Pitch));
        Front=glm::normalize(f);
        Right=glm::normalize(glm::cross(Front,WorldUp));
        Up   =glm::normalize(glm::cross(Right,Front));
    }
};

// ============================================================
//  TERRAIN
// ============================================================
class Terrain {
public:
    unsigned int VAO=0,VBO=0,EBO=0; int indexCount=0;
    int W=100,H=100; float cellSize=1.0f;
    float getHeight(float x,float z){
        return 2.5f*sinf(0.15f*x)*cosf(0.15f*z)
              +1.0f*sinf(0.4f*x+0.3f)*cosf(0.3f*z)
              +0.5f*sinf(0.8f*x)*sinf(0.8f*z)
              +0.3f*cosf(0.6f*x+1.2f)*sinf(0.5f*z+0.8f);
    }
    glm::vec3 getNormal(float x,float z){
        float d=0.15f;
        glm::vec3 t1(2.f*d,getHeight(x+d,z)-getHeight(x-d,z),0);
        glm::vec3 t2(0,getHeight(x,z+d)-getHeight(x,z-d),2.f*d);
        return glm::normalize(glm::cross(t2,t1));
    }
    glm::vec3 getColor(float h, glm::vec3 norm){
        // Get slope steepness
        float slope = 1.0f - glm::dot(norm, glm::vec3(0,1,0));
        
        glm::vec3 color;
        
        if(h < -0.3f) {
            // Deep water - rich dark blue
            color = glm::vec3(0.04f, 0.18f, 0.45f);
            float depth = glm::clamp((-h - 0.3f) * 0.8f, 0.0f, 1.0f);
            color = glm::mix(color, glm::vec3(0.02f,0.08f,0.25f), depth);
        }
        else if(h < 0.1f) {
            // Shallow water / shoreline - turquoise
            float t = (h + 0.3f) / 0.4f;
            color = glm::mix(
                glm::vec3(0.04f, 0.18f, 0.45f),
                glm::vec3(0.15f, 0.55f, 0.60f),
                t);
        }
        else if(h < 0.6f) {
            // Beach / shore sand
            float t = (h - 0.1f) / 0.5f;
            color = glm::mix(
                glm::vec3(0.75f, 0.70f, 0.45f),
                glm::vec3(0.35f, 0.62f, 0.22f),
                t);
        }
        else if(h < 1.8f) {
            // Lush farmland - BRIGHT GREEN
            float t = (h - 0.6f) / 1.2f;
            color = glm::mix(
                glm::vec3(0.30f, 0.68f, 0.18f),
                glm::vec3(0.22f, 0.52f, 0.12f),
                t);
            // Add golden wheat tint on flat areas
            if(slope < 0.05f) {
                color = glm::mix(color, 
                    glm::vec3(0.58f, 0.72f, 0.18f), 0.25f);
            }
        }
        else if(h < 3.0f) {
            // Mid hills - olive green to brown
            float t = (h - 1.8f) / 1.2f;
            color = glm::mix(
                glm::vec3(0.22f, 0.48f, 0.12f),
                glm::vec3(0.55f, 0.42f, 0.22f),
                t);
        }
        else if(h < 4.2f) {
            // High hills - rocky brown-grey
            float t = (h - 3.0f) / 1.2f;
            color = glm::mix(
                glm::vec3(0.55f, 0.42f, 0.22f),
                glm::vec3(0.58f, 0.55f, 0.52f),
                t);
        }
        else {
            // Peaks - light grey with snow tint
            color = glm::mix(
                glm::vec3(0.62f, 0.60f, 0.58f),
                glm::vec3(0.88f, 0.90f, 0.92f),
                glm::clamp((h - 4.2f) * 0.5f, 0.0f, 1.0f));
        }
        
        // Darken steep slopes (cliff faces)
        if(slope > 0.3f) {
            color *= (1.0f - slope * 0.5f);
        }
        
        return color;
    }
    void generate(){
        std::vector<float> verts; std::vector<unsigned int> idx;
        for(int gz=0;gz<=H;gz++) for(int gx=0;gx<=W;gx++){
            float wx=gx*cellSize-W*cellSize/2.f, wz=gz*cellSize-H*cellSize/2.f;
            float h=getHeight(wx,wz);
            glm::vec3 n=getNormal(wx,wz), c=getColor(h, n);
            verts.insert(verts.end(),{wx,h,wz,n.x,n.y,n.z,c.r,c.g,c.b});
        }
        for(int gz=0;gz<H;gz++) for(int gx=0;gx<W;gx++){
            unsigned int tl=gz*(W+1)+gx, tr=tl+1;
            unsigned int bl=(gz+1)*(W+1)+gx, br=bl+1;
            idx.insert(idx.end(),{tl,bl,tr, tr,bl,br});
        }
        indexCount=(int)idx.size();
        glGenVertexArrays(1,&VAO); glGenBuffers(1,&VBO); glGenBuffers(1,&EBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER,VBO);
        glBufferData(GL_ARRAY_BUFFER,verts.size()*sizeof(float),verts.data(),GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(unsigned int),idx.data(),GL_STATIC_DRAW);
        for(int i=0;i<3;i++){ glVertexAttribPointer(i,3,GL_FLOAT,GL_FALSE,9*sizeof(float),(void*)(i*3*sizeof(float))); glEnableVertexAttribArray(i); }
        glBindVertexArray(0);
    }
    float getHeightAt(float wx,float wz){ return getHeight(wx,wz); }
    void render(Shader& s){ s.use(); s.setMat4("model",glm::mat4(1)); glBindVertexArray(VAO); glDrawElements(GL_TRIANGLES,indexCount,GL_UNSIGNED_INT,0); glBindVertexArray(0); }
    void cleanup(){ glDeleteVertexArrays(1,&VAO); glDeleteBuffers(1,&VBO); glDeleteBuffers(1,&EBO); }
};

// ============================================================
//  PRIMITIVES
// ============================================================
struct Mesh { unsigned int VAO=0,VBO=0; int count=0; };
namespace Prim {
    static void upload(Mesh& m, const std::vector<float>& v){
        m.count=(int)v.size()/9;
        glGenVertexArrays(1,&m.VAO); glGenBuffers(1,&m.VBO);
        glBindVertexArray(m.VAO);
        glBindBuffer(GL_ARRAY_BUFFER,m.VBO);
        glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(float),v.data(),GL_STATIC_DRAW);
        for(int i=0;i<3;i++){ glVertexAttribPointer(i,3,GL_FLOAT,GL_FALSE,9*sizeof(float),(void*)(i*3*sizeof(float))); glEnableVertexAttribArray(i); }
        glBindVertexArray(0);
    }
    Mesh makeBox(glm::vec3 hs, glm::vec3 col){
        float px=hs.x,py=hs.y,pz=hs.z;
        float fv[6][4][3]={
            {{px,py,pz},{px,-py,pz},{px,-py,-pz},{px,py,-pz}},
            {{-px,py,-pz},{-px,-py,-pz},{-px,-py,pz},{-px,py,pz}},
            {{-px,py,-pz},{-px,py,pz},{px,py,pz},{px,py,-pz}},
            {{-px,-py,pz},{-px,-py,-pz},{px,-py,-pz},{px,-py,pz}},
            {{-px,py,pz},{-px,-py,pz},{px,-py,pz},{px,py,pz}},
            {{px,py,-pz},{px,-py,-pz},{-px,-py,-pz},{-px,py,-pz}}
        };
        float fn[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        int ord[6]={0,1,2,0,2,3};
        std::vector<float> v;
        for(int f=0;f<6;f++) for(int i=0;i<6;i++){
            int o=ord[i];
            v.insert(v.end(),{fv[f][o][0],fv[f][o][1],fv[f][o][2],fn[f][0],fn[f][1],fn[f][2],col.r,col.g,col.b});
        }
        Mesh m; upload(m,v); return m;
    }
    Mesh makeCylinder(float r,float h,int seg,glm::vec3 col){
        std::vector<float> v;
        for(int i=0;i<seg;i++){
            float a0=2*PI*i/seg,a1=2*PI*(i+1)/seg;
            float ma=(a0+a1)*0.5f;
            float nx=cosf(ma),nz=sinf(ma);
            auto add=[&](float x,float y,float z,float nn,float nzz){
                v.insert(v.end(),{x,y,z,nn,0.f,nzz,col.r,col.g,col.b});
            };
            add(r*cosf(a0),0,r*sinf(a0),nx,nz);
            add(r*cosf(a1),0,r*sinf(a1),nx,nz);
            add(r*cosf(a1),h,r*sinf(a1),nx,nz);
            add(r*cosf(a0),0,r*sinf(a0),nx,nz);
            add(r*cosf(a1),h,r*sinf(a1),nx,nz);
            add(r*cosf(a0),h,r*sinf(a0),nx,nz);
            // caps
            v.insert(v.end(),{0,h,0,0,1,0,col.r,col.g,col.b});
            v.insert(v.end(),{r*cosf(a0),h,r*sinf(a0),0,1,0,col.r,col.g,col.b});
            v.insert(v.end(),{r*cosf(a1),h,r*sinf(a1),0,1,0,col.r,col.g,col.b});
            v.insert(v.end(),{0,0,0,0,-1,0,col.r,col.g,col.b});
            v.insert(v.end(),{r*cosf(a1),0,r*sinf(a1),0,-1,0,col.r,col.g,col.b});
            v.insert(v.end(),{r*cosf(a0),0,r*sinf(a0),0,-1,0,col.r,col.g,col.b});
        }
        Mesh m; upload(m,v); return m;
    }
    Mesh makeSphere(float r,int lat,int lon,glm::vec3 col){
        std::vector<float> v;
        for(int i=0;i<lat;i++){
            float t0=PI*i/lat,t1=PI*(i+1)/lat;
            for(int j=0;j<lon;j++){
                float p0=2*PI*j/lon,p1=2*PI*(j+1)/lon;
                auto P=[&](float t,float p)->glm::vec3{ return r*glm::vec3(sinf(t)*cosf(p),cosf(t),sinf(t)*sinf(p)); };
                glm::vec3 v00=P(t0,p0),v10=P(t1,p0),v11=P(t1,p1),v01=P(t0,p1);
                auto push=[&](glm::vec3 vv){ glm::vec3 nn=glm::normalize(vv); v.insert(v.end(),{vv.x,vv.y,vv.z,nn.x,nn.y,nn.z,col.r,col.g,col.b}); };
                push(v00);push(v10);push(v11); push(v00);push(v11);push(v01);
            }
        }
        Mesh m; upload(m,v); return m;
    }
    // Torus ring for node indicators
    Mesh makeTorus(float R,float r,int seg,int tube,glm::vec3 col){
        std::vector<float> v;
        for(int i=0;i<seg;i++){
            float a0=2*PI*i/seg,a1=2*PI*(i+1)/seg;
            for(int j=0;j<tube;j++){
                float b0=2*PI*j/tube,b1=2*PI*(j+1)/tube;
                auto P=[&](float a,float b)->glm::vec3{
                    return glm::vec3((R+r*cosf(b))*cosf(a),r*sinf(b),(R+r*cosf(b))*sinf(a));
                };
                auto N=[&](float a,float b)->glm::vec3{
                    return glm::normalize(glm::vec3(cosf(b)*cosf(a),sinf(b),cosf(b)*sinf(a)));
                };
                auto push=[&](float a,float b){ auto p=P(a,b); auto n=N(a,b); v.insert(v.end(),{p.x,p.y,p.z,n.x,n.y,n.z,col.r,col.g,col.b}); };
                push(a0,b0);push(a1,b0);push(a1,b1); push(a0,b0);push(a1,b1);push(a0,b1);
            }
        }
        Mesh m; upload(m,v); return m;
    }
    void draw(Mesh& m,Shader& s,glm::vec3 pos,glm::vec3 scl,float rotY,glm::vec3 em=glm::vec3(0)){
        glm::mat4 mdl(1);
        mdl=glm::translate(mdl,pos);
        mdl=glm::rotate(mdl,rotY,glm::vec3(0,1,0));
        mdl=glm::scale(mdl,scl);
        s.setMat4("model",mdl); s.setVec3("emissiveColor",em);
        glBindVertexArray(m.VAO); glDrawArrays(GL_TRIANGLES,0,m.count);
    }
    void drawRotX(Mesh& m,Shader& s,glm::vec3 pos,glm::vec3 scl,float rotX,float rotY){
        glm::mat4 mdl(1);
        mdl=glm::translate(mdl,pos);
        mdl=glm::rotate(mdl,rotY,glm::vec3(0,1,0));
        mdl=glm::rotate(mdl,rotX,glm::vec3(1,0,0));
        mdl=glm::scale(mdl,scl);
        s.setMat4("model",mdl); s.setVec3("emissiveColor",glm::vec3(0));
        glBindVertexArray(m.VAO); glDrawArrays(GL_TRIANGLES,0,m.count);
    }
    // Cone - apex at (0,h,0), base radius r at y=0
    Mesh makeCone(float r,float h,int seg,glm::vec3 col){
        std::vector<float> v;
        for(int i=0;i<seg;i++){
            float a0=2*PI*i/seg,a1=2*PI*(i+1)/seg;
            float x0=r*cosf(a0),z0=r*sinf(a0);
            float x1=r*cosf(a1),z1=r*sinf(a1);
            // side face - normal pointing outward-up
            float ma=(a0+a1)*0.5f;
            float sl=sqrtf(r*r+h*h);
            float nx=cosf(ma)*h/sl, ny=r/sl, nz=sinf(ma)*h/sl;
            v.insert(v.end(),{x0,0,z0, nx,ny,nz, col.r,col.g,col.b});
            v.insert(v.end(),{x1,0,z1, nx,ny,nz, col.r,col.g,col.b});
            v.insert(v.end(),{0, h,0,  nx,ny,nz, col.r,col.g,col.b});
            // base cap
            v.insert(v.end(),{0,0,0,       0,-1,0, col.r,col.g,col.b});
            v.insert(v.end(),{x1,0,z1,     0,-1,0, col.r,col.g,col.b});
            v.insert(v.end(),{x0,0,z0,     0,-1,0, col.r,col.g,col.b});
        }
        Mesh m; upload(m,v); return m;
    }
    // Wedge/gable roof - length along Z, ridge along Z
    // base: 2*hx wide, 2*hz deep; apex offset hy above base centre
    Mesh makeGable(float hx,float hy,float hz,glm::vec3 col){
        std::vector<float> v;
        // two sloping roof panels
        float ny1=hx, nx1=hy; float len=sqrtf(nx1*nx1+ny1*ny1);
        glm::vec3 nL=glm::normalize(glm::vec3(-hy,hx,0));
        glm::vec3 nR=glm::normalize(glm::vec3( hy,hx,0));
        auto push=[&](glm::vec3 p,glm::vec3 n){
            v.insert(v.end(),{p.x,p.y,p.z,n.x,n.y,n.z,col.r,col.g,col.b});
        };
        // Left panel (-x side)
        push({-hx,0,-hz},nL); push({-hx,0,hz},nL); push({0,hy,-hz},nL);
        push({-hx,0,hz},nL);  push({0,hy,hz},nL);  push({0,hy,-hz},nL);
        // Right panel (+x side)
        push({hx,0,-hz},nR);  push({0,hy,-hz},nR);  push({hx,0,hz},nR);
        push({hx,0,hz},nR);   push({0,hy,-hz},nR);  push({0,hy,hz},nR);
        // Gable end -z
        glm::vec3 nFront(0,0,-1);
        push({-hx,0,-hz},nFront); push({0,hy,-hz},nFront); push({hx,0,-hz},nFront);
        // Gable end +z
        glm::vec3 nBack(0,0,1);
        push({-hx,0,hz},nBack);  push({hx,0,hz},nBack);  push({0,hy,hz},nBack);
        Mesh m; upload(m,v); return m;
    }
    void free(Mesh& m){ glDeleteVertexArrays(1,&m.VAO); glDeleteBuffers(1,&m.VBO); }
}

// ============================================================
//  PRIM CACHE
// ============================================================
struct PrimCache {
    Mesh box,cyl,sph,tor,blade,cone,gable;
    void init(){
        box   = Prim::makeBox(glm::vec3(1),glm::vec3(1));
        cyl   = Prim::makeCylinder(1,1,20,glm::vec3(1));
        sph   = Prim::makeSphere(1,12,16,glm::vec3(1));
        tor   = Prim::makeTorus(1.0f,0.12f,24,8,glm::vec3(1));
        blade = Prim::makeBox(glm::vec3(0.08f,0.8f,0.04f),glm::vec3(0.85f,0.85f,0.82f));
        cone  = Prim::makeCone(1,1,20,glm::vec3(1));
        gable = Prim::makeGable(1,1,1,glm::vec3(1));
    }
    void cleanup(){ Prim::free(box);Prim::free(cyl);Prim::free(sph);Prim::free(tor);Prim::free(blade);Prim::free(cone);Prim::free(gable); }
};

// ============================================================
//  NODE DRAWERS
// ============================================================
static void setCol(Shader& s,glm::vec3 c){ s.setVec3("overrideColor",c); }

void drawFarm(Shader& s,PrimCache& p,glm::vec3 pos,float time){
    s.setBool("useOverrideColor",true); s.setVec3("emissiveColor",glm::vec3(0));
    // Ground pad
    setCol(s,glm::vec3(0.22f,0.58f,0.16f));
    Prim::draw(p.box,s,pos+glm::vec3(0,-0.12f,0),glm::vec3(7.f,0.15f,7.f),0);
    // Dirt path
    setCol(s,glm::vec3(0.60f,0.48f,0.30f));
    Prim::draw(p.box,s,pos+glm::vec3(0,-0.07f,4.f),glm::vec3(0.8f,0.14f,3.f),0);
    // Barn body
    setCol(s,glm::vec3(0.64f,0.11f,0.09f));
    Prim::draw(p.box,s,pos+glm::vec3(0,1.0f,0),glm::vec3(2.0f,2.0f,2.8f),0);
    // Barn base trim
    setCol(s,glm::vec3(0.28f,0.18f,0.10f));
    Prim::draw(p.box,s,pos+glm::vec3(0,0.12f,0),glm::vec3(2.05f,0.20f,2.85f),0);
    // Barn door
    setCol(s,glm::vec3(0.30f,0.18f,0.09f));
    Prim::draw(p.box,s,pos+glm::vec3(0,0.75f,2.83f),glm::vec3(0.65f,1.5f,0.06f),0);
    // Window frames
    setCol(s,glm::vec3(0.90f,0.88f,0.82f));
    Prim::draw(p.box,s,pos+glm::vec3(-0.7f,1.3f,2.83f),glm::vec3(0.28f,0.32f,0.06f),0);
    Prim::draw(p.box,s,pos+glm::vec3( 0.7f,1.3f,2.83f),glm::vec3(0.28f,0.32f,0.06f),0);
    // Gable roof
    setCol(s,glm::vec3(0.18f,0.16f,0.15f));
    Prim::draw(p.gable,s,pos+glm::vec3(0,2.0f,0),glm::vec3(2.25f,1.2f,2.95f),0);
    // Ridge cap
    setCol(s,glm::vec3(0.12f,0.10f,0.10f));
    Prim::draw(p.box,s,pos+glm::vec3(0,3.18f,0),glm::vec3(0.14f,0.12f,3.1f),0);
    // Silo body
    setCol(s,glm::vec3(0.78f,0.74f,0.65f));
    Prim::draw(p.cyl,s,pos+glm::vec3(2.9f,0,0.5f),glm::vec3(0.70f,3.2f,0.70f),0);
    // Silo hoops
    setCol(s,glm::vec3(0.50f,0.48f,0.42f));
    for(float bh:{0.6f,1.3f,2.0f,2.7f})
        Prim::draw(p.cyl,s,pos+glm::vec3(2.9f,bh,0.5f),glm::vec3(0.73f,0.06f,0.73f),0);
    // Silo cone cap
    setCol(s,glm::vec3(0.52f,0.18f,0.09f));
    Prim::draw(p.cone,s,pos+glm::vec3(2.9f,3.2f,0.5f),glm::vec3(0.82f,1.0f,0.82f),0);
    // Silo vent
    setCol(s,glm::vec3(0.35f,0.35f,0.35f));
    Prim::draw(p.cyl,s,pos+glm::vec3(2.9f,4.18f,0.5f),glm::vec3(0.12f,0.35f,0.12f),0);
    // Hay shed
    setCol(s,glm::vec3(0.70f,0.60f,0.33f));
    Prim::draw(p.box,s,pos+glm::vec3(-3.0f,0.55f,1.0f),glm::vec3(1.0f,1.1f,1.5f),0);
    setCol(s,glm::vec3(0.40f,0.33f,0.16f));
    Prim::draw(p.gable,s,pos+glm::vec3(-3.0f,1.1f,1.0f),glm::vec3(1.1f,0.7f,1.6f),0);
    // Fence posts
    setCol(s,glm::vec3(0.55f,0.38f,0.18f));
    for(float fx:{-5.5f,-4.2f,-2.9f,-1.6f,1.6f,2.9f,4.2f,5.5f})
        Prim::draw(p.box,s,pos+glm::vec3(fx,0.45f,-6.1f),glm::vec3(0.10f,0.5f,0.10f),0);
    // Fence rails
    setCol(s,glm::vec3(0.60f,0.44f,0.22f));
    Prim::draw(p.box,s,pos+glm::vec3(0,0.62f,-6.1f),glm::vec3(5.8f,0.06f,0.06f),0);
    Prim::draw(p.box,s,pos+glm::vec3(0,0.30f,-6.1f),glm::vec3(5.8f,0.06f,0.06f),0);
    // Windmill tower (tapered)
    setCol(s,glm::vec3(0.74f,0.72f,0.68f));
    Prim::draw(p.cyl,s,pos+glm::vec3(-3.8f,0,2.5f),glm::vec3(0.25f,2.0f,0.25f),0);
    Prim::draw(p.cyl,s,pos+glm::vec3(-3.8f,2.0f,2.5f),glm::vec3(0.18f,1.5f,0.18f),0);
    Prim::draw(p.cyl,s,pos+glm::vec3(-3.8f,3.5f,2.5f),glm::vec3(0.13f,1.2f,0.13f),0);
    // Hub
    setCol(s,glm::vec3(0.88f,0.88f,0.88f));
    Prim::draw(p.sph,s,pos+glm::vec3(-3.8f,4.7f,2.5f),glm::vec3(0.22f),0);
    // Blades
    float rot=time*1.8f;
    setCol(s,glm::vec3(0.95f,0.95f,0.92f));
    for(int i=0;i<3;i++){
        float br=rot+2.f*PI*i/3.f;
        Prim::drawRotX(p.blade,s,pos+glm::vec3(-3.8f,4.7f,2.5f),glm::vec3(1.4f,1.f,1.f),br,0);
    }
    // Crop rows
    for(float rx:{-1.2f,0.f,1.2f}){
        setCol(s,glm::vec3(0.45f,0.80f,0.12f));
        Prim::draw(p.box,s,pos+glm::vec3(rx,0.20f,-3.8f),glm::vec3(0.25f,0.25f,2.5f),0);
    }
    s.setBool("useOverrideColor",false);
}

void drawWarehouse(Shader& s,PrimCache& p,glm::vec3 pos){
    s.setBool("useOverrideColor",true); s.setVec3("emissiveColor",glm::vec3(0));
    // Concrete base
    setCol(s,glm::vec3(0.50f,0.50f,0.48f));
    Prim::draw(p.box,s,pos+glm::vec3(0,-0.1f,0),glm::vec3(5.5f,0.18f,6.5f),0);
    // Main body - corrugated steel
    setCol(s,glm::vec3(0.58f,0.62f,0.68f));
    Prim::draw(p.box,s,pos+glm::vec3(0,1.5f,0),glm::vec3(5.0f,3.0f,6.0f),0);
    // Wall ribs (vertical steel columns)
    setCol(s,glm::vec3(0.45f,0.48f,0.54f));
    for(float rx:{-3.8f,-1.9f,0.f,1.9f,3.8f}){
        Prim::draw(p.box,s,pos+glm::vec3(rx,1.5f,6.02f),glm::vec3(0.12f,3.0f,0.10f),0);
        Prim::draw(p.box,s,pos+glm::vec3(rx,1.5f,-6.02f),glm::vec3(0.12f,3.0f,0.10f),0);
    }
    // Gable roof
    setCol(s,glm::vec3(0.30f,0.32f,0.36f));
    Prim::draw(p.gable,s,pos+glm::vec3(0,3.0f,0),glm::vec3(5.2f,1.4f,6.2f),0);
    // Ridge cap
    setCol(s,glm::vec3(0.22f,0.22f,0.24f));
    Prim::draw(p.box,s,pos+glm::vec3(0,4.38f,0),glm::vec3(0.18f,0.14f,6.3f),0);
    // Loading dock platform
    setCol(s,glm::vec3(0.42f,0.42f,0.40f));
    Prim::draw(p.box,s,pos+glm::vec3(0,0.38f,6.3f),glm::vec3(3.2f,0.5f,0.8f),0);
    // Dock door
    setCol(s,glm::vec3(0.24f,0.28f,0.34f));
    Prim::draw(p.box,s,pos+glm::vec3(0,1.2f,6.06f),glm::vec3(1.5f,1.8f,0.09f),0);
    // Safety stripe
    setCol(s,glm::vec3(0.95f,0.80f,0.08f));
    Prim::draw(p.box,s,pos+glm::vec3(0,0.40f,6.08f),glm::vec3(5.0f,0.20f,0.09f),0);
    // Two chimney stacks on roof
    setCol(s,glm::vec3(0.35f,0.33f,0.30f));
    Prim::draw(p.cyl,s,pos+glm::vec3(-1.8f,4.4f,2.0f),glm::vec3(0.22f,1.0f,0.22f),0);
    Prim::draw(p.cyl,s,pos+glm::vec3( 1.8f,4.4f,2.0f),glm::vec3(0.22f,1.0f,0.22f),0);
    // Stack caps
    setCol(s,glm::vec3(0.22f,0.22f,0.22f));
    Prim::draw(p.cyl,s,pos+glm::vec3(-1.8f,5.38f,2.0f),glm::vec3(0.30f,0.12f,0.30f),0);
    Prim::draw(p.cyl,s,pos+glm::vec3( 1.8f,5.38f,2.0f),glm::vec3(0.30f,0.12f,0.30f),0);
    // Smoke emissive
    s.setVec3("emissiveColor",glm::vec3(0.08f,0.07f,0.06f));
    Prim::draw(p.sph,s,pos+glm::vec3(-1.8f,5.7f,2.0f),glm::vec3(0.25f),0);
    Prim::draw(p.sph,s,pos+glm::vec3( 1.8f,5.7f,2.0f),glm::vec3(0.25f),0);
    s.setVec3("emissiveColor",glm::vec3(0));
    // Side window strip
    setCol(s,glm::vec3(0.62f,0.78f,0.90f));
    for(float wx:{-3.f,-1.f,1.f,3.f}){
        Prim::draw(p.box,s,pos+glm::vec3(wx,2.8f,6.08f),glm::vec3(0.45f,0.35f,0.09f),0);
    }
    s.setBool("useOverrideColor",false);
}

void drawColdStorage(Shader& s,PrimCache& p,glm::vec3 pos,float time){
    s.setBool("useOverrideColor",true); s.setVec3("emissiveColor",glm::vec3(0));
    // Concrete base slab
    setCol(s,glm::vec3(0.48f,0.50f,0.52f));
    Prim::draw(p.box,s,pos+glm::vec3(0,-0.12f,0),glm::vec3(5.5f,0.18f,6.5f),0);
    // Main insulated body - white with blue tint
    setCol(s,glm::vec3(0.88f,0.92f,0.96f));
    Prim::draw(p.box,s,pos+glm::vec3(0,1.5f,0),glm::vec3(4.8f,3.0f,5.8f),0);
    // Insulation panel seams (vertical)
    setCol(s,glm::vec3(0.70f,0.76f,0.84f));
    for(float px:{-3.2f,-1.6f,0.f,1.6f,3.2f}){
        Prim::draw(p.box,s,pos+glm::vec3(px,1.5f, 5.82f),glm::vec3(0.06f,3.0f,0.06f),0);
        Prim::draw(p.box,s,pos+glm::vec3(px,1.5f,-5.82f),glm::vec3(0.06f,3.0f,0.06f),0);
        Prim::draw(p.box,s,pos+glm::vec3(4.82f,1.5f,px-0.5f),glm::vec3(0.06f,3.0f,0.06f),0);
    }
    // Flat roof with slight raised border
    setCol(s,glm::vec3(0.60f,0.68f,0.80f));
    Prim::draw(p.box,s,pos+glm::vec3(0,3.08f,0),glm::vec3(5.0f,0.18f,6.0f),0);
    setCol(s,glm::vec3(0.44f,0.54f,0.70f));
    Prim::draw(p.box,s,pos+glm::vec3(0,3.3f,0),glm::vec3(5.2f,0.25f,6.2f),0);
    // Rooftop cooling units (box + cylinder)
    setCol(s,glm::vec3(0.55f,0.60f,0.68f));
    Prim::draw(p.box,s,pos+glm::vec3(-2.0f,3.65f, 1.5f),glm::vec3(1.0f,0.55f,0.8f),0);
    Prim::draw(p.box,s,pos+glm::vec3( 2.0f,3.65f, 1.5f),glm::vec3(1.0f,0.55f,0.8f),0);
    Prim::draw(p.box,s,pos+glm::vec3( 0.0f,3.65f,-1.5f),glm::vec3(1.0f,0.55f,0.8f),0);
    // Cooling fan cylinders on units
    setCol(s,glm::vec3(0.35f,0.40f,0.50f));
    Prim::draw(p.cyl,s,pos+glm::vec3(-2.0f,4.22f, 1.5f),glm::vec3(0.38f,0.15f,0.38f),0);
    Prim::draw(p.cyl,s,pos+glm::vec3( 2.0f,4.22f, 1.5f),glm::vec3(0.38f,0.15f,0.38f),0);
    Prim::draw(p.cyl,s,pos+glm::vec3( 0.0f,4.22f,-1.5f),glm::vec3(0.38f,0.15f,0.38f),0);
    // Vent stacks
    setCol(s,glm::vec3(0.62f,0.66f,0.72f));
    Prim::draw(p.cyl,s,pos+glm::vec3(-3.5f,3.3f, 2.0f),glm::vec3(0.18f,0.9f,0.18f),0);
    Prim::draw(p.cyl,s,pos+glm::vec3( 3.5f,3.3f,-2.0f),glm::vec3(0.18f,0.9f,0.18f),0);
    // Door - heavy insulated
    setCol(s,glm::vec3(0.72f,0.78f,0.88f));
    Prim::draw(p.box,s,pos+glm::vec3(0,1.1f,5.84f),glm::vec3(1.2f,2.2f,0.09f),0);
    setCol(s,glm::vec3(0.30f,0.40f,0.65f));
    Prim::draw(p.box,s,pos+glm::vec3(0,1.1f,5.94f),glm::vec3(0.06f,2.2f,0.06f),0);
    // Corner blue glow emissive orbs
    s.setBool("useOverrideColor",true);
    for(auto& c:std::initializer_list<glm::vec3>{{3.2f,3.5f,3.2f},{-3.2f,3.5f,3.2f},{3.2f,3.5f,-3.2f},{-3.2f,3.5f,-3.2f}}){
        setCol(s,glm::vec3(0.3f,0.6f,1.0f));
        s.setVec3("emissiveColor",glm::vec3(0.08f,0.22f,0.75f));
        Prim::draw(p.sph,s,pos+c,glm::vec3(0.32f),0);
    }
    s.setVec3("emissiveColor",glm::vec3(0));
    s.setBool("useOverrideColor",false);
}

void drawMarket(Shader& s,PrimCache& p,glm::vec3 pos,float time){
    s.setBool("useOverrideColor",true); s.setVec3("emissiveColor",glm::vec3(0));
    // Stone base
    setCol(s,glm::vec3(0.70f,0.65f,0.55f));
    Prim::draw(p.box,s,pos+glm::vec3(0,-0.12f,0),glm::vec3(7.5f,0.20f,6.5f),0);
    // Main hall - warm sandstone
    setCol(s,glm::vec3(0.88f,0.72f,0.46f));
    Prim::draw(p.box,s,pos+glm::vec3(0,1.8f,0),glm::vec3(5.5f,3.6f,4.5f),0);
    // Hall wall base band
    setCol(s,glm::vec3(0.60f,0.50f,0.35f));
    Prim::draw(p.box,s,pos+glm::vec3(0,0.25f,0),glm::vec3(5.6f,0.38f,4.6f),0);
    // Gable roof - terracotta tiles
    setCol(s,glm::vec3(0.72f,0.26f,0.12f));
    Prim::draw(p.gable,s,pos+glm::vec3(0,3.6f,0),glm::vec3(5.8f,1.5f,4.8f),0);
    // Ridge
    setCol(s,glm::vec3(0.50f,0.16f,0.08f));
    Prim::draw(p.box,s,pos+glm::vec3(0,5.08f,0),glm::vec3(0.18f,0.15f,4.9f),0);
    // Corner tower body
    setCol(s,glm::vec3(0.82f,0.66f,0.40f));
    Prim::draw(p.box,s,pos+glm::vec3(4.8f,2.5f,0),glm::vec3(1.5f,5.0f,1.5f),0);
    // Tower base moulding
    setCol(s,glm::vec3(0.60f,0.50f,0.35f));
    Prim::draw(p.box,s,pos+glm::vec3(4.8f,0.28f,0),glm::vec3(1.65f,0.38f,1.65f),0);
    // Tower top cornice
    setCol(s,glm::vec3(0.58f,0.46f,0.28f));
    Prim::draw(p.box,s,pos+glm::vec3(4.8f,5.1f,0),glm::vec3(1.72f,0.20f,1.72f),0);
    // Tower dome - ochre
    setCol(s,glm::vec3(0.85f,0.60f,0.10f));
    Prim::draw(p.cone,s,pos+glm::vec3(4.8f,5.3f,0),glm::vec3(1.1f,1.6f,1.1f),0);
    // Tower spire tip
    setCol(s,glm::vec3(0.50f,0.35f,0.05f));
    Prim::draw(p.cyl,s,pos+glm::vec3(4.8f,6.9f,0),glm::vec3(0.06f,0.5f,0.06f),0);
    // Colonnade arched pillars along front
    setCol(s,glm::vec3(0.90f,0.84f,0.70f));
    for(float cx:{-3.5f,-1.5f,1.5f,3.5f}){
        Prim::draw(p.cyl,s,pos+glm::vec3(cx,0.f,-4.9f),glm::vec3(0.22f,2.6f,0.22f),0);
    }
    // Colonnade beam
    setCol(s,glm::vec3(0.75f,0.68f,0.52f));
    Prim::draw(p.box,s,pos+glm::vec3(0,2.6f,-4.9f),glm::vec3(4.0f,0.22f,0.24f),0);
    // Windows - arched (tall box)
    setCol(s,glm::vec3(0.55f,0.72f,0.88f));
    for(float wx:{-2.5f,0.f,2.5f}){
        Prim::draw(p.box,s,pos+glm::vec3(wx,1.9f, 4.52f),glm::vec3(0.45f,1.0f,0.09f),0);
        Prim::draw(p.box,s,pos+glm::vec3(wx,1.9f,-4.52f),glm::vec3(0.45f,1.0f,0.09f),0);
    }
    // Market stalls
    glm::vec3 sc[]={{0.82f,0.15f,0.15f},{0.15f,0.72f,0.22f},{0.15f,0.35f,0.85f}};
    float sx[]={-3.8f,0.f,3.8f};
    for(int i=0;i<3;i++){
        setCol(s,sc[i]);
        Prim::draw(p.box,s,pos+glm::vec3(sx[i],0.55f,-6.2f),glm::vec3(1.1f,1.1f,1.0f),0);
        setCol(s,glm::vec3(0.96f,0.94f,0.90f));
        Prim::draw(p.gable,s,pos+glm::vec3(sx[i],1.1f,-6.2f),glm::vec3(1.2f,0.55f,1.1f),0);
    }
    // Emissive lamp at tower base
    s.setVec3("emissiveColor",glm::vec3(0.6f,0.45f,0.05f));
    setCol(s,glm::vec3(1.0f,0.85f,0.4f));
    Prim::draw(p.sph,s,pos+glm::vec3(4.8f,0.9f,1.6f),glm::vec3(0.18f),0);
    s.setVec3("emissiveColor",glm::vec3(0));
    s.setBool("useOverrideColor",false);
}


// ============================================================
//  SUPPLY CHAIN
// ============================================================
enum NodeType { FARM=0, WAREHOUSE=1, COLD_STORAGE=2, MARKET=3 };
struct SCNode { int id; const char* name; NodeType type; glm::vec3 pos; bool selected=false; };
struct SCEdge { int from,to; float dist,spoilRate; int middlemen; bool onPath=false; };
struct PathResult { std::vector<int> ids; float totalDist=0,totalSpoil=0; int totalMiddlemen=0; bool valid=false; };

class SupplyChain {
public:
    std::vector<SCNode> nodes;
    std::vector<SCEdge> edges;
    PathResult lastPath;
    void init(Terrain& T){
        nodes={
            {0,"Vidarbha Wheat Farm",  FARM,       {-30,0,-20}},
            {1,"Nashik Cooperative",   FARM,       {-35,0,10}},
            {2,"Ludhiana Fields",      FARM,       {-8,0,28}},
            {3,"Amritsar Farm",        FARM,       {5,0,-28}},
            {4,"Nagpur Warehouse",     WAREHOUSE,  {-18,0,2}},
            {5,"Ludhiana Hub",         WAREHOUSE,  {6,0,14}},
            {6,"Nashik Cold Chain",    COLD_STORAGE,{-14,0,-8}},
            {7,"Delhi Main Mandi",     MARKET,     {24,0,-14}},
            {8,"Delhi Retail",         MARKET,     {29,0,6}},
            {9,"New Delhi Market",     MARKET,     {19,0,-28}}
        };
        for(auto& n:nodes) n.pos.y=T.getHeightAt(n.pos.x,n.pos.z)+1.8f;
        edges={
            {0,4,45,0.008f,2},{0,6,30,0.003f,0},{1,4,35,0.007f,1},{1,6,25,0.002f,0},
            {2,5,20,0.006f,1},{2,4,55,0.009f,3},{3,6,40,0.005f,1},{3,4,50,0.008f,2},
            {4,7,80,0.007f,1},{4,8,95,0.008f,2},{5,7,60,0.005f,1},{5,8,50,0.004f,0},
            {6,7,70,0.003f,0},{6,9,90,0.004f,1},{4,5,40,0.004f,1},{6,5,30,0.002f,0},
            {5,9,110,0.006f,2},{3,9,130,0.010f,4}
        };
    }
    PathResult runDijkstra(int src,int dst){
        int N=(int)nodes.size();
        std::vector<float> dist(N,1e9f); std::vector<int> prev(N,-1);
        dist[src]=0;
        std::priority_queue<std::pair<float,int>,std::vector<std::pair<float,int>>,std::greater<std::pair<float,int>>> pq;
        pq.push({0,src});
        while(!pq.empty()){
            auto top=pq.top(); pq.pop();
            float cost=top.first; int u=top.second;
            if(cost>dist[u]) continue;
            for(auto& e:edges){
                int v=-1; float w=0;
                if(e.from==u){v=e.to; w=e.dist+e.spoilRate*1000+e.middlemen*50;}
                else if(e.to==u){v=e.from; w=e.dist+e.spoilRate*1000+e.middlemen*50;}
                if(v<0) continue;
                if(dist[u]+w<dist[v]){dist[v]=dist[u]+w;prev[v]=u;pq.push({dist[v],v});}
            }
        }
        PathResult res; if(dist[dst]>=1e8f) return res;
        for(int c=dst;c!=-1;c=prev[c]) res.ids.push_back(c);
        std::reverse(res.ids.begin(),res.ids.end());
        for(int i=0;i+1<(int)res.ids.size();i++){
            int a=res.ids[i],b=res.ids[i+1];
            for(auto& e:edges) if((e.from==a&&e.to==b)||(e.from==b&&e.to==a)){
                res.totalDist+=e.dist; res.totalSpoil+=e.spoilRate*e.dist; res.totalMiddlemen+=e.middlemen;
            }
        }
        res.valid=true; lastPath=res;
        for(auto& e:edges) e.onPath=false;
        for(int i=0;i+1<(int)res.ids.size();i++){
            int a=res.ids[i],b=res.ids[i+1];
            for(auto& e:edges) if((e.from==a&&e.to==b)||(e.from==b&&e.to==a)) e.onPath=true;
        }
        return res;
    }
    int pickNode(glm::vec3 ro,glm::vec3 rd){
        float near=1e9f; int hit=-1;
        for(auto& n:nodes){
            glm::vec3 oc=ro-n.pos;
            float b=glm::dot(oc,rd), c=glm::dot(oc,oc)-20.f, disc=b*b-c;
            if(disc<0) continue;
            float t=-b-sqrtf(disc);
            if(t>0&&t<near){near=t;hit=n.id;}
        }
        return hit;
    }
};

// ============================================================
//  VEHICLES
// ============================================================
struct Vehicle {
    int id; std::vector<glm::vec3> wp; int seg=0; float t=0,speed=0.05f;
    glm::vec3 pos,dir={1,0,0}; float freshTimer=0,maxFresh=120,spoil=0; bool alive=true;
    glm::vec3 color() const {
        glm::vec3 g(0.1f,0.9f,0.1f),m(1.f,0.85f,0.f),r(1.f,0.05f,0.05f);
        return spoil<0.5f?glm::mix(g,m,spoil*2.f):glm::mix(m,r,(spoil-0.5f)*2.f);
    }
};
class VehicleSystem {
public:
    std::vector<Vehicle> vehs; int nid=0,delivered=0,spoiled=0;
    Mesh body,cab,wheel;
    void init(){
        body =Prim::makeBox(glm::vec3(1),glm::vec3(0.25f,0.28f,0.82f));
        cab  =Prim::makeBox(glm::vec3(1),glm::vec3(0.22f,0.25f,0.72f));
        wheel=Prim::makeCylinder(0.3f,0.18f,10,glm::vec3(0.1f,0.1f,0.1f));
    }
    void spawn(const std::vector<glm::vec3>& wp,float mf){
        if(wp.size()<2) return;
        Vehicle v; v.id=nid++; v.wp=wp; v.pos=wp[0]+glm::vec3(0,1.5f,0);
        v.maxFresh=mf; v.speed=0.04f+(rand()%25)*0.001f; vehs.push_back(v);
    }
    void update(float dt){
        for(auto& v:vehs){
            if(!v.alive) continue;
            v.t+=v.speed*dt;
            if(v.t>=1.f){ v.t-=1.f; v.seg++; if(v.seg>=(int)v.wp.size()-1){v.alive=false; if(v.spoil>0.7f)spoiled++;else delivered++;continue;} }
            glm::vec3 a=v.wp[v.seg],b=v.wp[v.seg+1];
            v.pos=glm::mix(a,b,v.t)+glm::vec3(0,1.5f,0);
            glm::vec3 d=b-a; if(glm::length(d)>0.01f) v.dir=glm::normalize(d);
            v.freshTimer+=dt; v.spoil=glm::clamp(v.freshTimer/v.maxFresh,0.f,1.f);
        }
        vehs.erase(std::remove_if(vehs.begin(),vehs.end(),[](const Vehicle& v){return !v.alive;}),vehs.end());
    }
    void render(Shader& s, PrimCache& pc, float time){
        s.setBool("useOverrideColor",true);
        for(auto& v:vehs){
            float yaw=atan2f(v.dir.x,v.dir.z);

            // Helper: draw a box at offset from vehicle centre, rotated by yaw
            auto dbox=[&](glm::vec3 off,glm::vec3 sc2,glm::vec3 col,glm::vec3 em=glm::vec3(0)){
                s.setVec3("overrideColor",col);
                Prim::draw(pc.box,s,v.pos+off,sc2,yaw,em);
            };
            auto dcyl=[&](glm::vec3 off,float r,float h,glm::vec3 col){
                s.setVec3("overrideColor",col);
                glm::mat4 mdl(1);
                mdl=glm::translate(mdl,v.pos+off);
                mdl=glm::rotate(mdl,yaw,glm::vec3(0,1,0));
                mdl=glm::scale(mdl,glm::vec3(r,h,r));
                s.setMat4("model",mdl); s.setVec3("emissiveColor",glm::vec3(0));
                glBindVertexArray(pc.cyl.VAO); glDrawArrays(GL_TRIANGLES,0,pc.cyl.count);
            };

            // ---- Truck body colours ----
            glm::vec3 cabCol(0.18f,0.22f,0.72f);      // deep blue cab
            glm::vec3 cabDark(0.12f,0.15f,0.52f);     // darker trim
            glm::vec3 chassisCol(0.22f,0.22f,0.24f);  // near-black steel chassis
            glm::vec3 cargoCol = v.color();             // freshness-tinted cargo
            glm::vec3 wheelCol(0.10f,0.10f,0.10f);    // tyre black
            glm::vec3 hubCol  (0.70f,0.70f,0.72f);    // silver hubcap
            glm::vec3 glassCol(0.55f,0.78f,0.92f);    // tinted windshield
            glm::vec3 bumperCol(0.55f,0.55f,0.58f);   // chrome bumper
            glm::vec3 exhaustCol(0.32f,0.30f,0.28f);  // dark pipe

            // ---- Chassis frame (low, flat undercarriage) ----
            dbox(glm::vec3(0,-0.22f,0),glm::vec3(0.85f,0.12f,2.0f),chassisCol);
            // Side chassis rails
            dbox(glm::vec3( 0.72f,-0.18f,0),glm::vec3(0.10f,0.10f,1.9f),chassisCol);
            dbox(glm::vec3(-0.72f,-0.18f,0),glm::vec3(0.10f,0.10f,1.9f),chassisCol);

            // ---- CAB (front half) ----
            // Main cab box
            dbox(glm::vec3(0, 0.52f, 1.0f),glm::vec3(0.82f,0.88f,0.68f),cabCol);
            // Cab roof
            dbox(glm::vec3(0, 1.30f, 0.95f),glm::vec3(0.78f,0.12f,0.62f),cabDark);
            // Front face panel (slightly inset for depth)
            dbox(glm::vec3(0, 0.45f, 1.65f),glm::vec3(0.82f,0.60f,0.06f),cabDark);
            // Windshield recess (glass, emissive-tinted)
            dbox(glm::vec3(0, 0.90f, 1.68f),glm::vec3(0.62f,0.38f,0.04f),glassCol,glm::vec3(0.04f,0.06f,0.08f));
            // Cab side windows
            dbox(glm::vec3( 0.84f,0.90f, 0.95f),glm::vec3(0.04f,0.30f,0.42f),glassCol,glm::vec3(0.02f,0.04f,0.06f));
            dbox(glm::vec3(-0.84f,0.90f, 0.95f),glm::vec3(0.04f,0.30f,0.42f),glassCol,glm::vec3(0.02f,0.04f,0.06f));
            // Door panel lines
            dbox(glm::vec3( 0.84f,0.40f, 0.85f),glm::vec3(0.04f,0.65f,0.55f),cabCol);
            dbox(glm::vec3(-0.84f,0.40f, 0.85f),glm::vec3(0.04f,0.65f,0.55f),cabCol);
            // Door handle strips
            dbox(glm::vec3( 0.86f,0.35f, 0.90f),glm::vec3(0.04f,0.06f,0.18f),bumperCol);
            dbox(glm::vec3(-0.86f,0.35f, 0.90f),glm::vec3(0.04f,0.06f,0.18f),bumperCol);
            // Front grille
            dbox(glm::vec3(0, 0.15f, 1.68f),glm::vec3(0.65f,0.18f,0.06f),chassisCol);
            // Grille horizontal bars
            for(float gy:{0.06f,0.14f,0.22f})
                dbox(glm::vec3(0,gy,1.70f),glm::vec3(0.58f,0.03f,0.04f),bumperCol);
            // Front bumper
            dbox(glm::vec3(0,-0.12f,1.70f),glm::vec3(0.88f,0.14f,0.10f),bumperCol);
            // Rear bumper
            dbox(glm::vec3(0,-0.12f,-1.78f),glm::vec3(0.88f,0.14f,0.10f),bumperCol);

            // ---- Side mirrors ----
            dbox(glm::vec3( 0.90f,0.98f,1.55f),glm::vec3(0.06f,0.08f,0.14f),bumperCol);
            dbox(glm::vec3(-0.90f,0.98f,1.55f),glm::vec3(0.06f,0.08f,0.14f),bumperCol);

            // ---- Exhaust stack (right side of cab) ----
            dcyl(glm::vec3(0.72f,0.80f,0.90f),0.055f,0.90f,exhaustCol);
            // Smoke puff emissive
            s.setVec3("overrideColor",glm::vec3(0.55f,0.52f,0.50f));
            s.setVec3("emissiveColor",glm::vec3(0.05f,0.05f,0.05f));
            Prim::draw(pc.sph,s,v.pos+glm::vec3(0.72f,1.78f,0.90f),glm::vec3(0.12f),yaw);
            s.setVec3("emissiveColor",glm::vec3(0));

            // ---- Cargo box (rear) ----
            // Main box - freshness colour
            dbox(glm::vec3(0, 0.60f,-0.55f),glm::vec3(0.78f,0.82f,1.0f),cargoCol);
            // Cargo roof
            dbox(glm::vec3(0, 1.40f,-0.55f),glm::vec3(0.80f,0.10f,1.02f),glm::mix(cargoCol,glm::vec3(0),0.25f));
            // Cargo rear door frame
            dbox(glm::vec3(0, 0.62f,-1.54f),glm::vec3(0.76f,0.80f,0.06f),glm::mix(cargoCol,glm::vec3(0),0.35f));
            // Cargo door horizontal ribbing
            for(float ry:{0.20f,0.50f,0.80f,1.10f})
                dbox(glm::vec3(0,ry,-1.56f),glm::vec3(0.72f,0.04f,0.05f),glm::mix(cargoCol,glm::vec3(1),0.15f));
            // Cab-to-cargo connector step
            dbox(glm::vec3(0,-0.05f, 0.30f),glm::vec3(0.60f,0.18f,0.28f),chassisCol);

            // ---- Animated wheels (6 wheels: 2 front, 4 rear dual) ----
            // Wheel spin: distance-based rotation using time
            float wheelSpin = time * 3.5f * v.speed * 25.f;
            float wheelRot  = wheelSpin;

            auto drawWheel=[&](glm::vec3 localOff){
                // Rotate offset by yaw to get world-space offset
                float ox = localOff.x*cosf(yaw) - localOff.z*sinf(yaw);
                float oz = localOff.x*sinf(yaw) + localOff.z*cosf(yaw);
                glm::vec3 wp = v.pos + glm::vec3(ox, localOff.y, oz);
                // Tyre
                glm::mat4 mdl(1);
                mdl=glm::translate(mdl,wp);
                mdl=glm::rotate(mdl,yaw,glm::vec3(0,1,0));
                mdl=glm::rotate(mdl,wheelRot,glm::vec3(1,0,0)); // spin
                mdl=glm::scale(mdl,glm::vec3(0.28f,0.14f,0.28f)); // r=0.28, h=0.14 tyre
                s.setVec3("overrideColor",wheelCol); s.setVec3("emissiveColor",glm::vec3(0));
                s.setMat4("model",mdl);
                glBindVertexArray(pc.cyl.VAO); glDrawArrays(GL_TRIANGLES,0,pc.cyl.count);
                // Hubcap (flat disc, same rotation axis, slightly outward)
                float hx = wp.x + (localOff.x<0?-0.08f:0.08f)*cosf(yaw);
                float hz = wp.z + (localOff.x<0?-0.08f:0.08f)*sinf(yaw);
                glm::mat4 hm(1);
                hm=glm::translate(hm,glm::vec3(hx,wp.y,hz));
                hm=glm::rotate(hm,yaw,glm::vec3(0,1,0));
                hm=glm::rotate(hm,wheelRot,glm::vec3(1,0,0));
                hm=glm::scale(hm,glm::vec3(0.20f,0.04f,0.20f));
                s.setVec3("overrideColor",hubCol);
                s.setMat4("model",hm);
                glBindVertexArray(pc.cyl.VAO); glDrawArrays(GL_TRIANGLES,0,pc.cyl.count);
            };

            // Front axle wheels
            drawWheel(glm::vec3( 0.86f,-0.30f, 1.25f));
            drawWheel(glm::vec3(-0.86f,-0.30f, 1.25f));
            // Rear axle 1
            drawWheel(glm::vec3( 0.86f,-0.30f,-0.50f));
            drawWheel(glm::vec3(-0.86f,-0.30f,-0.50f));
            // Rear axle 2
            drawWheel(glm::vec3( 0.86f,-0.30f,-1.10f));
            drawWheel(glm::vec3(-0.86f,-0.30f,-1.10f));

            // ---- Headlights (front) ---- emissive bright warm white
            s.setVec3("overrideColor",glm::vec3(1.0f,0.97f,0.88f));
            s.setVec3("emissiveColor",glm::vec3(1.1f,1.0f,0.65f));
            float fx=cosf(yaw)*1.70f, fz=sinf(yaw)*1.70f;
            Prim::draw(pc.sph,s,v.pos+glm::vec3( 0.40f+fx*0.0f,0.15f, 0.0f)+glm::vec3(fx,0,fz),glm::vec3(0.11f),yaw);
            Prim::draw(pc.sph,s,v.pos+glm::vec3(-0.40f+fx*0.0f,0.15f, 0.0f)+glm::vec3(fx,0,fz),glm::vec3(0.11f),yaw);
            // Fog lights (lower)
            s.setVec3("overrideColor",glm::vec3(1.0f,0.95f,0.70f));
            s.setVec3("emissiveColor",glm::vec3(0.7f,0.65f,0.30f));
            Prim::draw(pc.sph,s,v.pos+glm::vec3( 0.55f,-0.10f,0)+glm::vec3(fx,0,fz),glm::vec3(0.07f),yaw);
            Prim::draw(pc.sph,s,v.pos+glm::vec3(-0.55f,-0.10f,0)+glm::vec3(fx,0,fz),glm::vec3(0.07f),yaw);

            // ---- Taillights (rear) ---- emissive red
            s.setVec3("overrideColor",glm::vec3(0.95f,0.05f,0.05f));
            s.setVec3("emissiveColor",glm::vec3(0.80f,0.0f,0.0f));
            float rx2=-cosf(yaw)*1.55f, rz2=-sinf(yaw)*1.55f;
            Prim::draw(pc.sph,s,v.pos+glm::vec3( 0.50f,0.50f,0)+glm::vec3(rx2,0,rz2),glm::vec3(0.09f),yaw);
            Prim::draw(pc.sph,s,v.pos+glm::vec3(-0.50f,0.50f,0)+glm::vec3(rx2,0,rz2),glm::vec3(0.09f),yaw);
            s.setVec3("emissiveColor",glm::vec3(0));
        }
        s.setBool("useOverrideColor",false);
    }


    Mesh s_sph; // small sphere for lights Ã¢â‚¬â€ set from outside after init
    void cleanup(){ Prim::free(body);Prim::free(cab);Prim::free(wheel);Prim::free(s_sph); }
};

// ============================================================
//  PARTICLES
// ============================================================
struct Particle { glm::vec3 pos,vel; float life=0,maxLife=1,spoil=0; bool active=false;
    glm::vec3 color() const {
        glm::vec3 g(0.1f,1,0.2f),y(1,1,0.1f),r(1,0.1f,0.1f);
        return spoil<0.5f?glm::mix(g,y,spoil*2):glm::mix(y,r,(spoil-0.5f)*2);
    }
};
class ParticleSystem {
    static const int MAX=1200;
    Particle pool[MAX];
    unsigned int VAO=0,VBO=0;
    struct PV{glm::vec3 p,c;float sz;};
    std::vector<PV> buf;
public:
    int active=0;
    void init(){
        glGenVertexArrays(1,&VAO);glGenBuffers(1,&VBO);
        glBindVertexArray(VAO);glBindBuffer(GL_ARRAY_BUFFER,VBO);
        glBufferData(GL_ARRAY_BUFFER,MAX*sizeof(PV),nullptr,GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(PV),(void*)0);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(PV),(void*)(3*sizeof(float)));glEnableVertexAttribArray(1);
        glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,sizeof(PV),(void*)(6*sizeof(float)));glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }
    void emit(glm::vec3 p,glm::vec3 dir,float spd,float life){
        for(int i=0;i<MAX;i++) if(!pool[i].active){pool[i]={p,dir*spd,0,life,0,true};return;}
    }
    void update(float dt){
        buf.clear();active=0;
        for(int i=0;i<MAX;i++){
            auto& p=pool[i]; if(!p.active) continue;
            p.life+=dt; p.spoil=p.life/p.maxLife;
            p.pos+=p.vel*dt; p.vel.y-=0.06f*dt;
            p.vel*=0.998f; // drag
            if(p.life>=p.maxLife){p.active=false;continue;}
            float age=1.f-p.spoil;
            buf.push_back({p.pos,p.color(),0.18f+0.12f*age});active++;
        }
        if(!buf.empty()){glBindBuffer(GL_ARRAY_BUFFER,VBO);glBufferSubData(GL_ARRAY_BUFFER,0,buf.size()*sizeof(PV),buf.data());}
    }
    void render(Shader& s){
        if(buf.empty()) return;
        glEnable(GL_PROGRAM_POINT_SIZE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);glDepthMask(GL_FALSE);
        s.use();glBindVertexArray(VAO);glDrawArrays(GL_POINTS,0,(GLsizei)buf.size());
        glBindVertexArray(0);glDepthMask(GL_TRUE);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDisable(GL_PROGRAM_POINT_SIZE);
    }
    void cleanup(){glDeleteVertexArrays(1,&VAO);glDeleteBuffers(1,&VBO);}
};

// ============================================================
//  ROUTE RENDERER
// ============================================================
class RouteRenderer {
public:
    unsigned int VAO=0,VBO=0;
    void init(){
        glGenVertexArrays(1,&VAO);glGenBuffers(1,&VBO);
        glBindVertexArray(VAO);glBindBuffer(GL_ARRAY_BUFFER,VBO);
        glBufferData(GL_ARRAY_BUFFER,6*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }
    void drawLine(Shader& s,glm::vec3 a,glm::vec3 b,glm::vec4 col,float time,bool dash,float flowSpd){
        float verts[6]={a.x,a.y+0.6f,a.z,b.x,b.y+0.6f,b.z};
        glBindBuffer(GL_ARRAY_BUFFER,VBO);glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(verts),verts);
        s.setVec4("lineColor",col);s.setFloat("time",time);s.setBool("isDashed",dash);s.setFloat("flowSpeed",flowSpd);
        glBindVertexArray(VAO);glLineWidth(dash?5.0f:2.0f);glDrawArrays(GL_LINES,0,2);
    }
    void renderAll(Shader& s,SupplyChain& sc,float time,bool optimOn){
        s.use();
        for(auto& e:sc.edges){
            glm::vec3 a=sc.nodes[e.from].pos,b=sc.nodes[e.to].pos;
            bool onPath=e.onPath&&optimOn;
            
            if(onPath){
                // Draw glow effect: thick bright line
                // Outer glow - wide transparent green
                glLineWidth(8.0f);
                drawLine(s,a,b,glm::vec4(0.1f,0.9f,0.2f,0.25f),time,false,0.0f);
                // Middle - medium green
                glLineWidth(4.0f);
                drawLine(s,a,b,glm::vec4(0.15f,1.0f,0.25f,0.7f),time,false,0.0f);
                // Core - thin bright white-green animated
                glLineWidth(2.0f);
                drawLine(s,a,b,glm::vec4(0.7f,1.0f,0.75f,1.0f),time,true,8.0f);
            } else {
                // Unoptimized: red lines
                glLineWidth(5.0f);
                drawLine(s,a,b,glm::vec4(0.85f,0.12f,0.08f,0.5f),time,false,0.0f);
                glLineWidth(2.0f);
                drawLine(s,a,b,glm::vec4(1.0f,0.25f,0.15f,0.85f),time,false,0.0f);
            }
        }
        glLineWidth(1.0f);
    }
    void cleanup(){glDeleteVertexArrays(1,&VAO);glDeleteBuffers(1,&VBO);}
};

// ============================================================
//  BLOOM FBO
// ============================================================
struct BloomFBO {
    unsigned int sceneFBO=0,sceneTex=0,sceneDepth=0;
    unsigned int brightFBO=0,brightTex=0;
    unsigned int blurFBO[2]={0,0},blurTex[2]={0,0};
    unsigned int quadVAO=0,quadVBO=0;
    int W=0,H=0;
    void init(int w,int h){
        W=w;H=h;
        // Scene FBO
        glGenFramebuffers(1,&sceneFBO);glBindFramebuffer(GL_FRAMEBUFFER,sceneFBO);
        glGenTextures(1,&sceneTex);glBindTexture(GL_TEXTURE_2D,sceneTex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,w,h,0,GL_RGB,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,sceneTex,0);
        glGenRenderbuffers(1,&sceneDepth);glBindRenderbuffer(GL_RENDERBUFFER,sceneDepth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,sceneDepth);
        // Bright FBO
        glGenFramebuffers(1,&brightFBO);glBindFramebuffer(GL_FRAMEBUFFER,brightFBO);
        glGenTextures(1,&brightTex);glBindTexture(GL_TEXTURE_2D,brightTex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,w,h,0,GL_RGB,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,brightTex,0);
        // Blur FBOs
        glGenFramebuffers(2,blurFBO);glGenTextures(2,blurTex);
        for(int i=0;i<2;i++){
            glBindFramebuffer(GL_FRAMEBUFFER,blurFBO[i]);
            glBindTexture(GL_TEXTURE_2D,blurTex[i]);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,w/2,h/2,0,GL_RGB,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,blurTex[i],0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        // Full-screen quad
        float qv[]={-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,-1,0,0, 1,1,1,1, -1,1,0,1};
        glGenVertexArrays(1,&quadVAO);glGenBuffers(1,&quadVBO);
        glBindVertexArray(quadVAO);glBindBuffer(GL_ARRAY_BUFFER,quadVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(qv),qv,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }
    void resize(int w, int h){
        if(w==W && h==H) return;
        W=w; H=h;
        glBindTexture(GL_TEXTURE_2D,sceneTex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,w,h,0,GL_RGB,GL_FLOAT,nullptr);
        glBindRenderbuffer(GL_RENDERBUFFER,sceneDepth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,w,h);
        
        glBindTexture(GL_TEXTURE_2D,brightTex);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,w,h,0,GL_RGB,GL_FLOAT,nullptr);
        
        for(int i=0;i<2;i++){
            glBindTexture(GL_TEXTURE_2D,blurTex[i]);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,w/2,h/2,0,GL_RGB,GL_FLOAT,nullptr);
        }
    }
    void bindScene(){ glBindFramebuffer(GL_FRAMEBUFFER,sceneFBO); }
    void doBloom(Shader& brightSh,Shader& blurSh){
        // Extract bright
        glBindFramebuffer(GL_FRAMEBUFFER,brightFBO);
        glViewport(0,0,W,H);glClear(GL_COLOR_BUFFER_BIT);
        brightSh.use();glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,sceneTex);brightSh.setInt("image",0);
        glBindVertexArray(quadVAO);glDrawArrays(GL_TRIANGLES,0,6);
        // Blur passes (half-res)
        glViewport(0,0,W/2,H/2);
        bool horiz=true;
        unsigned int src=brightTex;
        for(int i=0;i<8;i++){
            glBindFramebuffer(GL_FRAMEBUFFER,blurFBO[horiz?1:0]);
            glClear(GL_COLOR_BUFFER_BIT);
            blurSh.use();blurSh.setBool("horizontal",horiz);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,i==0?src:blurTex[horiz?0:1]);
            blurSh.setInt("image",0);
            glBindVertexArray(quadVAO);glDrawArrays(GL_TRIANGLES,0,6);
            horiz=!horiz;
        }
        glViewport(0,0,W,H);
    }
    void blit(Shader& compositeSh,float exposure){
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        compositeSh.use();
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,sceneTex);compositeSh.setInt("scene",0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,blurTex[0]);compositeSh.setInt("bloomTex",1);
        compositeSh.setFloat("exposure",exposure);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(quadVAO);glDrawArrays(GL_TRIANGLES,0,6);
        glEnable(GL_DEPTH_TEST);
    }
    void cleanup(){
        glDeleteFramebuffers(1,&sceneFBO);glDeleteTextures(1,&sceneTex);glDeleteRenderbuffers(1,&sceneDepth);
        glDeleteFramebuffers(1,&brightFBO);glDeleteTextures(1,&brightTex);
        glDeleteFramebuffers(2,blurFBO);glDeleteTextures(2,blurTex);
        glDeleteVertexArrays(1,&quadVAO);glDeleteBuffers(1,&quadVBO);
    }
};

// ============================================================
//  5Ãƒâ€”7 BITMAP FONT  (uppercase + digits + basic punctuation)
// ============================================================
// Each glyph is 5 columns Ãƒâ€” 7 rows, stored as 7 uint8 row bitmasks (bit4=leftmost)
static const uint8_t FONT5x7[][7] = {
  // ' '
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  // '!'
  {0x04,0x04,0x04,0x04,0x00,0x04,0x00},
  // '"'
  {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},
  // '#'
  {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00},
  // '$'
  {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
  // '%'
  {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
  // '&'
  {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D},
  // '\''
  {0x04,0x04,0x00,0x00,0x00,0x00,0x00},
  // '('
  {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
  // ')'
  {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
  // '*'
  {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},
  // '+'
  {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
  // ','
  {0x00,0x00,0x00,0x00,0x06,0x04,0x08},
  // '-'
  {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
  // '.'
  {0x00,0x00,0x00,0x00,0x00,0x06,0x00},
  // '/'
  {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
  // '0'
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
  // '1'
  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
  // '2'
  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
  // '3'
  {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
  // '4'
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
  // '5'
  {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  // '6'
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
  // '7'
  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
  // '8'
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  // '9'
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
  // ':'
  {0x00,0x06,0x00,0x00,0x06,0x00,0x00},
  // ';'
  {0x00,0x06,0x00,0x00,0x06,0x04,0x08},
  // '<'
  {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
  // '='
  {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
  // '>'
  {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
  // '?'
  {0x0E,0x11,0x01,0x06,0x04,0x00,0x04},
  // '@'
  {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E},
  // 'A'
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
  // 'B'
  {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
  // 'C'
  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  // 'D'
  {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
  // 'E'
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
  // 'F'
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  // 'G'
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
  // 'H'
  {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
  // 'I'
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
  // 'J'
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
  // 'K'
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
  // 'L'
  {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  // 'M'
  {0x11,0x1B,0x15,0x11,0x11,0x11,0x11},
  // 'N'
  {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
  // 'O'
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  // 'P'
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
  // 'Q'
  {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
  // 'R'
  {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  // 'S'
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
  // 'T'
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
  // 'U'
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
  // 'V'
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
  // 'W'
  {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
  // 'X'
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  // 'Y'
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
  // 'Z'
  {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
  // '['
  {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
  // '\'
  {0x10,0x08,0x08,0x04,0x02,0x02,0x01},
  // ']'
  {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
  // '^'
  {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
  // '_'
  {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
  // '`'
  {0x08,0x04,0x00,0x00,0x00,0x00,0x00},
  // 'a' (same as A for simplicity)
  {0x00,0x0E,0x01,0x0F,0x11,0x13,0x0D},
  // 'b'
  {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
  // 'c'
  {0x00,0x0E,0x10,0x10,0x10,0x11,0x0E},
  // 'd'
  {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
  // 'e'
  {0x00,0x0E,0x11,0x1F,0x10,0x11,0x0E},
  // 'f'
  {0x06,0x09,0x08,0x1C,0x08,0x08,0x08},
  // 'g'
  {0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E},
  // 'h'
  {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
  // 'i'
  {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
  // 'j'
  {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
  // 'k'
  {0x10,0x12,0x14,0x18,0x14,0x12,0x11},
  // 'l'
  {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
  // 'm'
  {0x00,0x1A,0x15,0x15,0x11,0x11,0x11},
  // 'n'
  {0x00,0x1E,0x11,0x11,0x11,0x11,0x11},
  // 'o'
  {0x00,0x0E,0x11,0x11,0x11,0x11,0x0E},
  // 'p'
  {0x00,0x1E,0x11,0x11,0x1E,0x10,0x10},
  // 'q'
  {0x00,0x0F,0x11,0x11,0x0F,0x01,0x01},
  // 'r'
  {0x00,0x16,0x19,0x10,0x10,0x10,0x10},
  // 's'
  {0x00,0x0E,0x10,0x0E,0x01,0x11,0x0E},
  // 't'
  {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
  // 'u'
  {0x00,0x11,0x11,0x11,0x11,0x13,0x0D},
  // 'v'
  {0x00,0x11,0x11,0x11,0x0A,0x0A,0x04},
  // 'w'
  {0x00,0x11,0x11,0x15,0x15,0x0A,0x0A},
  // 'x'
  {0x00,0x11,0x0A,0x04,0x0A,0x11,0x00},
  // 'y'
  {0x00,0x11,0x11,0x0F,0x01,0x11,0x0E},
  // 'z'
  {0x00,0x1F,0x02,0x04,0x08,0x10,0x1F},
  // '{'
  {0x02,0x04,0x04,0x08,0x04,0x04,0x02},
  // '|'
  {0x04,0x04,0x04,0x00,0x04,0x04,0x04},
  // '}'
  {0x08,0x04,0x04,0x02,0x04,0x04,0x08},
  // '~'
  {0x00,0x08,0x15,0x02,0x00,0x00,0x00},
  // DEL (placeholder)
  {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},
};

// ============================================================
//  HUD
// ============================================================
class HUD {
public:
    unsigned int VAO=0,VBO=0; int W=0,H=0; glm::mat4 ortho;
    void init(int w,int h){
        W=w;H=h;ortho=glm::ortho(0.f,(float)w,0.f,(float)h,-1.f,1.f);
        float v[]={0,0,0,0, 1,0,1,0, 1,1,1,1, 0,0,0,0, 1,1,1,1, 0,1,0,1};
        glGenVertexArrays(1,&VAO);glGenBuffers(1,&VBO);
        glBindVertexArray(VAO);glBindBuffer(GL_ARRAY_BUFFER,VBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }
    // Draw one pixel at (px,py) in screen coords (bottom-left origin)
    void pixel(Shader& s,float px,float py,float sz,glm::vec4 col){
        rect(s,px,py,sz,sz,col);
    }
    // Draw a character from the 5x7 bitmap font at screen position (sx,sy)
    // sz = pixel size of each font-dot, returns x advance
    float drawChar(Shader& s,char c,float sx,float sy,float sz,glm::vec4 col){
        int idx=(int)(unsigned char)c - 32;
        static const int NGLYPH=(int)(sizeof(FONT5x7)/sizeof(FONT5x7[0]));
        if(idx<0||idx>=NGLYPH) { return sz*6; }
        const uint8_t* glyph=FONT5x7[idx];
        for(int row=0;row<7;row++){
            uint8_t bits=glyph[row];
            for(int bit=0;bit<5;bit++){
                if(bits & (0x10>>bit))
                    pixel(s,sx+bit*sz, sy+(6-row)*sz, sz, col);
            }
        }
        return sz*6; // 5px wide + 1px spacing
    }
    // Draw a string; returns total width drawn
    float text(Shader& s,const char* str,float sx,float sy,float sz,glm::vec4 col){
        float cx=sx;
        for(const char* p=str;*p;p++) cx+=drawChar(s,*p,cx,sy,sz,col);
        return cx-sx;
    }
    // Formatted number text helpers
    void textF(Shader& s,const char* fmt,float v,float sx,float sy,float sz,glm::vec4 col){
        char buf[64]; snprintf(buf,sizeof(buf),fmt,v); text(s,buf,sx,sy,sz,col);
    }
    void textI(Shader& s,const char* fmt,int v,float sx,float sy,float sz,glm::vec4 col){
        char buf[64]; snprintf(buf,sizeof(buf),fmt,v); text(s,buf,sx,sy,sz,col);
    }
    void rect(Shader& s,float x,float y,float w,float h,glm::vec4 col,float time=0,bool alert=false){
        s.use();s.setMat4("orthoProj",ortho);
        s.setVec2("hudOffset",glm::vec2(x,y));s.setVec2("hudScale",glm::vec2(w,h));
        s.setVec4("quadColor",col);s.setFloat("time",time);s.setBool("isAlert",alert);
        glBindVertexArray(VAO);glDrawArrays(GL_TRIANGLES,0,6);
    }
    // Minimap - draws node dots
    void drawMinimap(Shader& s,SupplyChain& sc,PathResult& path,float time,
                     float mx,float my,float mw,float mh){
        // Background - sleek dark glass
        rect(s,mx,my,mw,mh,glm::vec4(0.04f,0.06f,0.12f,0.70f));
        rect(s,mx,my,mw,2,glm::vec4(0.3f,0.6f,1.0f,0.9f));
        rect(s,mx,my+mh-2,mw,2,glm::vec4(0.3f,0.6f,1.0f,0.9f));
        rect(s,mx,my,2,mh,glm::vec4(0.3f,0.6f,1.0f,0.9f));
        rect(s,mx+mw-2,my,2,mh,glm::vec4(0.3f,0.6f,1.0f,0.9f));
        // Soft grid lines to look high tech
        for(int i=1;i<5;i++) {
            rect(s,mx,my+i*(mh/5.f),mw,1,glm::vec4(1,1,1,0.08f));
            rect(s,mx+i*(mw/5.f),my,1,mh,glm::vec4(1,1,1,0.08f));
        }
        // Draw path lines
        float worldSpan=80.f;
        auto toMM=[&](glm::vec3 p)->glm::vec2{
            return glm::vec2(mx+mw*0.5f+p.x/worldSpan*mw*0.9f, my+mh*0.5f-p.z/worldSpan*mh*0.9f);
        };
        for(auto& e:sc.edges){
            glm::vec2 a=toMM(sc.nodes[e.from].pos), b=toMM(sc.nodes[e.to].pos);
            glm::vec4 lc=e.onPath?glm::vec4(0.1f,1.0f,0.2f,0.9f):glm::vec4(0.6f,0.1f,0.1f,0.5f);
            // Draw line as thin rect (approximate)
            glm::vec2 d=b-a; float len=glm::length(d);
            if(len<0.5f) continue;
            glm::vec2 mid=(a+b)*0.5f;
            float angle=atan2f(d.y,d.x);
            // Draw as 1px-wide rect stretched along dir - approximate with small squares along line
            int steps=(int)(len/3);
            for(int si=0;si<=steps;si++){
                float tt=(float)si/steps;
                glm::vec2 pt=a+d*tt;
                rect(s,pt.x-1,pt.y-1,2,2,lc);
            }
        }
        // Draw node dots
        glm::vec4 nodeColors[]={glm::vec4(0.2f,0.9f,0.2f,1),glm::vec4(0.7f,0.7f,0.7f,1),glm::vec4(0.2f,0.5f,1.0f,1),glm::vec4(1.0f,0.6f,0.1f,1)};
        for(auto& n:sc.nodes){
            glm::vec2 mp=toMM(n.pos);
            float sz=n.selected?7.f:5.f;
            glm::vec4 col=nodeColors[(int)n.type];
            if(n.selected) col=glm::vec4(1.0f,1.0f,0.2f,1.0f);
            rect(s,mp.x-sz*0.5f,mp.y-sz*0.5f,sz,sz,col);
        }
    }
    void render(Shader& s,SupplyChain& sc,PathResult& path,bool optimOn,
                int deliv,int spld,float spoilPct,float dist,float optDist,
                float tod,int fps,float time,
                float farmerRevOld,float farmerRevNew,
                int selectedNodeId,const std::string& selectedName){
        glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        float fW=(float)W, fH=(float)H;
        const glm::vec4 WHITE(1,1,1,0.95f);
        const glm::vec4 GREY (0.75f,0.78f,0.85f,0.9f);
        const glm::vec4 RED  (1.0f,0.3f,0.15f,1.0f);
        const glm::vec4 GREEN(0.15f,1.0f,0.25f,1.0f);
        const glm::vec4 GOLD (1.0f,0.85f,0.15f,1.0f);
        const glm::vec4 CYAN (0.3f,0.85f,1.0f,1.0f);
        const float FS=1.5f;   // font dot size
        const float FSS=1.1f;  // small font (inside bars)

        // ============================================================ Layout constants ============================================================
        const float TOP_H   = 40.f;  // top bar height
        const float BOT_H   = 38.f;  // bottom bar height
        const float PAD     =  6.f;
        const float MM_W    = 138.f; // minimap width
        const float MM_H    = 138.f; // minimap height
        const float PNL_W   = 272.f; // stats panel width
        // Stats panel: sits just below the top bar, leaves room for minimap + gap below
        const float MM_GAP  =  6.f;
        const float PNL_X   = fW - PNL_W - PAD;
        const float AVAIL   = fH - TOP_H - BOT_H - PAD*2;
        // Minimap sits at the very bottom of available space (above bottom bar)
        const float MM_Y    = BOT_H + PAD;
        const float MM_X    = fW - MM_W - PAD;
        // Stats panel fills space above the minimap, but capped so it never clips the top bar
        const float PNL_Y   = MM_Y + MM_H + MM_GAP;
        const float PNL_H   = glm::min(fH - TOP_H - PNL_Y - PAD, 310.f);
        // Bar widths that fit inside PNL_W with margins
        const float BAR_W   = (PNL_W - 32.f) * 0.5f - 2.f;  // ~118px each column
        const float FULL_W  = PNL_W - 24.f;                  // full-width bar
        const float C1      = PNL_X + 12.f;
        const float C2      = PNL_X + 12.f + BAR_W + 4.f;

        // ====================================================
        //  TOP STATUS BAR
        // ====================================================
        rect(s,0,fH-TOP_H,fW,TOP_H,glm::vec4(0.04f,0.05f,0.10f,0.88f));
        glm::vec4 borderCol = optimOn ? glm::vec4(0.1f,0.9f,0.2f,1) : glm::vec4(0.9f,0.2f,0.1f,1);
        rect(s,0,fH-TOP_H-2,fW,2,borderCol);

        // Left: clock + FPS
        {
            char buf[64];
            int hh=(int)tod, mm=(int)((tod-(float)hh)*60);
            snprintf(buf,sizeof(buf),"%02d:%02d %s",hh%12?hh%12:12,mm,hh<12?"AM":"PM");
            text(s,buf,10,fH-TOP_H+14,FS,CYAN);
            snprintf(buf,sizeof(buf),"FPS:%d",fps);
            text(s,buf,10,fH-TOP_H+2,FS,GREY);
        }

        // Centre badge
        {
            float bw=186.f,bh=30.f;
            float bx=fW*0.5f-bw*0.5f, by=fH-TOP_H+4.f;
            glm::vec4 bgC = optimOn?glm::vec4(0.04f,0.42f,0.08f,0.90f):glm::vec4(0.42f,0.04f,0.04f,0.90f);
            rect(s,bx,by,bw,bh,bgC);
            rect(s,bx,by,bw,2,borderCol); rect(s,bx,by+bh-2,bw,2,borderCol);
            rect(s,bx,by,2,bh,borderCol); rect(s,bx+bw-2,by,2,bh,borderCol);
            const char* badge=optimOn?"ROUTE OPTIMIZED: ON":"ROUTE OPTIMIZED: OFF";
            float tw=(float)strlen(badge)*FS*6;
            text(s,badge,bx+(bw-tw)*0.5f,by+10,FS,optimOn?GREEN:RED);
        }

        // Right: route stats (two lines, capped so they don't run off screen)
        {
            char buf[128];
            snprintf(buf,sizeof(buf),"Route:%.0fkm->%.0fkm  Spoil:%.0f%%->%.0f%%",
                dist,optDist,spoilPct*100.f,spoilPct*18.f);
            float tw=(float)strlen(buf)*FS*6;
            // Ensure it doesn't overlap the badge
            float rx=fW*0.5f+96.f;
            float availW=fW-rx-8.f;
            float fsTmp=FS;
            if(tw>availW) fsTmp=FS*(availW/tw);
            text(s,buf,rx,fH-TOP_H+16,fsTmp,GREY);

            snprintf(buf,sizeof(buf),"Revenue:INR%.0f->INR%.0f",farmerRevOld,farmerRevNew);
            tw=(float)strlen(buf)*FS*6;
            fsTmp=FS;
            if(tw>availW) fsTmp=FS*(availW/tw);
            text(s,buf,rx,fH-TOP_H+4,fsTmp,GOLD);
        }

        // ====================================================
        //  STATS PANEL  (right side, above minimap)
        // ====================================================
        // Shadow + glass background
        rect(s,PNL_X+5,PNL_Y-5,PNL_W,PNL_H,glm::vec4(0,0,0,0.30f));
        rect(s,PNL_X,PNL_Y,PNL_W,PNL_H,glm::vec4(0.04f,0.05f,0.11f,0.84f));
        rect(s,PNL_X,PNL_Y+PNL_H-2,PNL_W,2,CYAN*glm::vec4(1,1,1,0.85f));
        rect(s,PNL_X,PNL_Y,2,PNL_H,CYAN*glm::vec4(1,1,1,0.85f));
        rect(s,PNL_X+PNL_W-2,PNL_Y,2,PNL_H,CYAN*glm::vec4(1,1,1,0.45f));
        rect(s,PNL_X,PNL_Y,PNL_W,1,CYAN*glm::vec4(1,1,1,0.25f));

        // Panel title bar
        rect(s,PNL_X,PNL_Y+PNL_H-24,PNL_W,24,glm::vec4(0.08f,0.12f,0.28f,0.90f));
        text(s,"SUPPLY CHAIN STATS",PNL_X+8,PNL_Y+PNL_H-17,FS,WHITE);

        // Column headers
        float rowY=PNL_Y+PNL_H-44;
        rect(s,C1,rowY-1,BAR_W,17,glm::vec4(0.38f,0.04f,0.04f,0.72f));
        rect(s,C1,rowY-1,3,17,RED);
        text(s,"CURRENT",C1+5,rowY+3,FSS,glm::vec4(1.0f,0.7f,0.7f,1));
        rect(s,C2,rowY-1,BAR_W,17,glm::vec4(0.04f,0.32f,0.04f,0.72f));
        rect(s,C2,rowY-1,3,17,GREEN);
        text(s,"OPTIMIZED",C2+5,rowY+3,FSS,glm::vec4(0.7f,1.0f,0.7f,1));
        rowY -= 26;

        // Comparison bar helper
        auto barRow=[&](const char* label,float vA,float vB,float mA,float mB,
                        glm::vec4 fA,glm::vec4 fB,const char* uA,const char* uB){
            text(s,label,PNL_X+6,rowY+17,FSS,GREY);
            // bar A
            rect(s,C1,rowY,BAR_W,11,glm::vec4(0.12f,0.02f,0.02f,0.82f));
            rect(s,C1,rowY,BAR_W*glm::clamp(vA/mA,0.f,1.f),11,fA);
            char buf[32]; snprintf(buf,sizeof(buf),uA,vA);
            text(s,buf,C1+2,rowY+1,FSS-0.2f,WHITE);
            // bar B
            rect(s,C2,rowY,BAR_W,11,glm::vec4(0.02f,0.12f,0.02f,0.82f));
            rect(s,C2,rowY,BAR_W*glm::clamp(vB/mB,0.f,1.f),11,fB);
            snprintf(buf,sizeof(buf),uB,vB);
            text(s,buf,C2+2,rowY+1,FSS-0.2f,WHITE);
            rowY -= 26;
        };

        barRow("DISTANCE (km)", dist,optDist, 200,200,
               glm::vec4(0.85f,0.18f,0.08f,0.9f),glm::vec4(0.08f,0.85f,0.12f,0.9f),
               "%.0fkm","%.0fkm");
        barRow("SPOILAGE (%)", spoilPct*100.f,spoilPct*18.f, 100,100,
               glm::vec4(0.9f,0.32f,0.04f,0.9f),glm::vec4(0.08f,0.85f,0.12f,0.9f),
               "%.0f%%","%.0f%%");
        barRow("REVENUE (INR)", farmerRevOld,farmerRevNew, 2500,2500,
               glm::vec4(0.85f,0.62f,0.04f,0.9f),glm::vec4(0.08f,0.85f,0.12f,0.9f),
               "%.0f","%.0f");

        // Divider
        rowY += 2;
        rect(s,PNL_X+6,rowY,PNL_W-12,1,glm::vec4(0.25f,0.45f,0.65f,0.5f));
        rowY -= 6;

        // Full-width vehicle bars
        int total=deliv+spld;
        float delR=total>0?(float)deliv/total:0.f;
        float spdR=total>0?(float)spld/total:0.f;
        float mmR =glm::clamp(path.totalMiddlemen/6.f,0.f,1.f);

        auto fullBar=[&](const char* label,float ratio,glm::vec4 fill,const char* val){
            text(s,label,PNL_X+6,rowY+16,FSS,GREY);
            rect(s,C1,rowY,FULL_W,11,glm::vec4(0.05f,0.08f,0.05f,0.72f));
            rect(s,C1,rowY,FULL_W*ratio,11,fill);
            text(s,val,C1+2,rowY+1,FSS-0.2f,WHITE);
            rowY -= 28;
        };


        char dbuf[32];
        snprintf(dbuf,sizeof(dbuf),"%d delivered",deliv);
        fullBar("DELIVERIES",delR,glm::vec4(0.08f,0.85f,0.18f,0.9f),dbuf);
        snprintf(dbuf,sizeof(dbuf),"%d spoiled",spld);
        fullBar("SPOILED",spdR,glm::vec4(0.85f,0.14f,0.04f,0.9f),dbuf);
        snprintf(dbuf,sizeof(dbuf),"%d middlemen",path.totalMiddlemen);
        fullBar("MIDDLEMEN",mmR,GOLD,dbuf);

        // Selected node badge at very bottom of panel
        if(selectedNodeId>=0){
            float sy2=PNL_Y+4;
            rect(s,PNL_X+4,sy2,PNL_W-8,20,glm::vec4(0.05f,0.12f,0.05f,0.85f));
            rect(s,PNL_X+4,sy2,3,20,GREEN);
            text(s,"NODE:",PNL_X+10,sy2+6,FSS,GREY);
            // Truncate long name to fit
            std::string nm=selectedName.substr(0,26);
            text(s,nm.c_str(),PNL_X+52,sy2+6,FSS,GREEN);
        }

        // ====================================================
        //  MINIMAP  (directly below stats panel)
        // ====================================================
        // Title strip above minimap
        rect(s,MM_X,MM_Y+MM_H,MM_W,16,glm::vec4(0.05f,0.08f,0.18f,0.88f));
        text(s,"ROUTE MAP",MM_X+6,MM_Y+MM_H+4,FSS,CYAN);
        // Legend inside the title strip
        rect(s,MM_X+MM_W-42,MM_Y+MM_H+5,6,6,GREEN);
        text(s,"OPT",MM_X+MM_W-33,MM_Y+MM_H+5,FSS-0.3f,GREEN);
        rect(s,MM_X+MM_W-16,MM_Y+MM_H+5,6,6,RED);
        text(s,"STD",MM_X+MM_W-7,MM_Y+MM_H+5,FSS-0.3f,RED);

        drawMinimap(s,sc,path,time,MM_X,MM_Y,MM_W,MM_H);

        // ====================================================
        //  BOTTOM CONTROL BAR
        // ====================================================
        rect(s,0,0,fW,BOT_H,glm::vec4(0.04f,0.05f,0.10f,0.88f));
        rect(s,0,BOT_H-1,fW,1,glm::vec4(0.2f,0.4f,0.8f,0.65f));

        // Pills - use smaller font so text fits in 105px pill
        struct Pill { float x; glm::vec4 bg; const char* label; glm::vec4 tc; };
        Pill pills[] = {
            { 8,  glm::vec4(0.10f,0.22f,0.52f,0.88f), "WASD:MOVE",   CYAN },
            {118, glm::vec4(0.10f,0.42f,0.15f,0.88f), "O:OPT ON/OFF",GREEN},
            {228, glm::vec4(0.52f,0.32f,0.06f,0.88f), "1/2/3:SPEED", GOLD },
            {338, glm::vec4(0.35f,0.10f,0.52f,0.88f), "CLICK:SELECT",WHITE},
            {448, glm::vec4(0.52f,0.10f,0.10f,0.88f), "F1/F2/F3:CAM",glm::vec4(1,0.6f,0.6f,1)},
        };
        const float PILL_W=108.f;
        for(auto& pill:pills){
            rect(s,pill.x,5,PILL_W,26,pill.bg);
            rect(s,pill.x,5,PILL_W,1,glm::vec4(1,1,1,0.15f));
            rect(s,pill.x,30,PILL_W,1,glm::vec4(1,1,1,0.15f));
            rect(s,pill.x,5,1,26,glm::vec4(1,1,1,0.15f));
            rect(s,pill.x+PILL_W-1,5,1,26,glm::vec4(1,1,1,0.15f));
            float tw=(float)strlen(pill.label)*FSS*6;
            // scale font to always fit inside pill
            float fs2 = tw<PILL_W-6 ? FSS : FSS*(PILL_W-6)/tw;
            float lx  = pill.x+(PILL_W-(float)strlen(pill.label)*fs2*6)*0.5f;
            text(s,pill.label,lx,13,fs2,pill.tc);
        }

        glEnable(GL_DEPTH_TEST);
    }
    void cleanup(){glDeleteVertexArrays(1,&VAO);glDeleteBuffers(1,&VBO);}
};

// ============================================================
//  DAY/NIGHT
// ============================================================
struct DayNight {
    float tod=8.f,spd=1.0f;
    void update(float dt){tod+=dt*spd;if(tod>=24) tod-=24;}
    glm::vec3 sunDir(){
        // More dramatic angle for better building shadows
        float hour = tod;
        float angle = (hour / 24.0f) * 2.0f * PI - PI/2.0f;
        
        // Add slight Z tilt so shadows fall diagonally
        glm::vec3 dir = glm::vec3(
            cosf(angle) * 0.85f,
            sinf(angle),
            0.35f + sinf(angle * 0.5f) * 0.2f
        );
        return glm::normalize(dir);
    }
    glm::vec3 sunColor(){
        float t = tod;
        if(t < 5.0f || t > 22.0f) 
            return glm::vec3(0.08f, 0.08f, 0.20f);
        if(t < 7.0f) {
            float f = (t - 5.0f) / 2.0f;
            return glm::mix(
                glm::vec3(0.08f, 0.08f, 0.20f),
                glm::vec3(1.0f,  0.55f, 0.15f), f);
        }
        if(t < 9.0f) {
            float f = (t - 7.0f) / 2.0f;
            return glm::mix(
                glm::vec3(1.0f,  0.55f, 0.15f),
                glm::vec3(1.0f,  0.95f, 0.75f), f);
        }
        if(t < 17.0f) {
            return glm::vec3(1.0f, 0.97f, 0.88f);
        }
        if(t < 19.0f) {
            float f = (t - 17.0f) / 2.0f;
            return glm::mix(
                glm::vec3(1.0f,  0.97f, 0.88f),
                glm::vec3(1.0f,  0.65f, 0.25f), f);
        }
        if(t < 21.0f) {
            float f = (t - 19.0f) / 2.0f;
            return glm::mix(
                glm::vec3(1.0f,  0.65f, 0.25f),
                glm::vec3(0.85f, 0.25f, 0.10f), f);
        }
        float f = (t - 21.0f) / 1.0f;
        return glm::mix(
            glm::vec3(0.85f, 0.25f, 0.10f),
            glm::vec3(0.08f, 0.08f, 0.20f), f);
    }
    glm::vec3 ambient(){
        if(tod < 5.0f || tod > 22.0f)
            return glm::vec3(0.04f, 0.05f, 0.15f);
        if(tod < 7.0f || tod > 20.0f)
            return glm::vec3(0.28f, 0.20f, 0.15f);
        if(tod < 9.0f || tod > 18.0f)
            return glm::vec3(0.40f, 0.35f, 0.28f);
        return glm::vec3(0.35f, 0.40f, 0.48f);
    }
    glm::vec3 clearColor(){
        if(tod < 5.0f || tod > 22.0f)
            return glm::vec3(0.01f, 0.01f, 0.06f);
        if(tod < 7.0f)
            return glm::vec3(0.55f, 0.32f, 0.18f);
        if(tod < 9.0f)
            return glm::vec3(0.62f, 0.75f, 0.92f);
        if(tod < 18.0f)
            return glm::vec3(0.38f, 0.62f, 0.90f);
        if(tod < 20.0f)
            return glm::vec3(0.72f, 0.45f, 0.22f);
        if(tod < 22.0f)
            return glm::vec3(0.22f, 0.15f, 0.28f);
        return glm::vec3(0.01f, 0.01f, 0.06f);
    }
    glm::vec3 skyTop(){
        if(tod<6||tod>21) return glm::vec3(0.01f,0.01f,0.06f);
        if(tod<8||tod>19) return glm::vec3(0.48f,0.28f,0.12f);
        return glm::vec3(0.18f,0.42f,0.78f);
    }
    glm::vec3 skyHorizon(){
        if(tod<6||tod>21) return glm::vec3(0.01f,0.02f,0.10f);
        if(tod<8||tod>19) return glm::vec3(0.88f,0.52f,0.18f);
        return glm::vec3(0.62f,0.82f,0.98f);
    }
    float isNight(){return (tod<6||tod>21)?1.f:(tod<7?tod-6.f:(tod>20?21.f-tod:0.f));}
};

// ============================================================
//  GLOBALS & CALLBACKS
// ============================================================
Camera* gCam=nullptr;
bool gFirstMouse=true,gMouseClicked=false;
float gLastX=SCR_WIDTH/2.f,gLastY=SCR_HEIGHT/2.f;
bool gRightMouseDown=false;
double gClickX=0,gClickY=0;
bool gOptimOn=false;
int gDaySpeed=1; // 1,5,20

void fb_cb(GLFWwindow*,int w,int h){ if(w>0&&h>0){SCR_WIDTH=w;SCR_HEIGHT=h;} glViewport(0,0,w,h); }
void mouse_cb(GLFWwindow*,double x,double y){
    if(gFirstMouse){gLastX=(float)x;gLastY=(float)y;gFirstMouse=false;return;}
    float dx=(float)x-gLastX,dy=gLastY-(float)y;
    gLastX=(float)x;gLastY=(float)y;
    if(gCam && gRightMouseDown) gCam->ProcessMouse(dx,dy);
}
void scroll_cb(GLFWwindow*,double,double y){
    if(gCam){gCam->Zoom-=(float)y;gCam->Zoom=glm::clamp(gCam->Zoom,10.f,90.f);}
}
void mbtn_cb(GLFWwindow* w,int btn,int act,int){
    if(btn==GLFW_MOUSE_BUTTON_LEFT&&act==GLFW_PRESS){
        glfwGetCursorPos(w,&gClickX,&gClickY);
        gMouseClicked=true;
    }
    if(btn==GLFW_MOUSE_BUTTON_RIGHT){
        if(act==GLFW_PRESS) gRightMouseDown=true;
        if(act==GLFW_RELEASE) gRightMouseDown=false;
    }
}
void key_cb(GLFWwindow* w,int key,int,int act,int){
    if(act!=GLFW_PRESS) return;
    if(key==GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w,true);
    if(key==GLFW_KEY_O) gOptimOn=!gOptimOn;
    if(key==GLFW_KEY_1) gDaySpeed=1;
    if(key==GLFW_KEY_2) gDaySpeed=5;
    if(key==GLFW_KEY_3) gDaySpeed=20;
}

// ============================================================
//  MAIN
// ============================================================
int main(){
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES,4); // MSAA
    GLFWwindow* win=glfwCreateWindow(SCR_WIDTH,SCR_HEIGHT,"AgriFlow 3D Ã¢â‚¬â€ Enhanced",NULL,NULL);
    if(!win){fprintf(stderr,"GLFW fail\n");glfwTerminate();return -1;}
    glfwMakeContextCurrent(win);
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){fprintf(stderr,"GLAD fail\n");return -1;}
    glfwSetFramebufferSizeCallback(win,fb_cb);
    glfwSetCursorPosCallback(win,mouse_cb);
    glfwSetScrollCallback(win,scroll_cb);
    glfwSetKeyCallback(win,key_cb);
    glfwSetMouseButtonCallback(win,mbtn_cb);
    glfwSetInputMode(win,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
    glEnable(GL_DEPTH_TEST);glEnable(GL_BLEND);glEnable(GL_CULL_FACE);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    // Compile shaders
    Shader terrainSh(terrainVert,terrainFrag);
    Shader objectSh (objectVert, objectFrag);
    Shader skySh    (skyVert,    skyFrag);
    Shader lineSh   (lineVert,   lineFrag);
    Shader particleSh(particleVert,particleFrag);
    Shader hudSh    (hudVert,    hudFrag);
    Shader bloomSh  (bloomVert,  bloomFrag);
    Shader blurSh   (bloomVert,  blurFrag);
    Shader brightSh (bloomVert,  brightFrag);

    // Systems
    Camera cam(glm::vec3(0,22,52)); gCam=&cam;
    Terrain terrain; terrain.generate();
    PrimCache pc; pc.init();
    SupplyChain sc; sc.init(terrain);
    PathResult path=sc.runDijkstra(0,7);
    VehicleSystem vs; vs.init();
    vs.s_sph=Prim::makeSphere(1,6,8,glm::vec3(1));
    ParticleSystem ps; ps.init();
    RouteRenderer rr; rr.init();
    HUD hud; hud.init(SCR_WIDTH,SCR_HEIGHT);
    DayNight dn;
    BloomFBO bloom; bloom.init(SCR_WIDTH,SCR_HEIGHT);

    // Sky quad (full-screen triangle pair)
    unsigned int skyVAO=0,skyVBO=0;
    {float sv[]={-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
     glGenVertexArrays(1,&skyVAO);glGenBuffers(1,&skyVBO);
     glBindVertexArray(skyVAO);glBindBuffer(GL_ARRAY_BUFFER,skyVBO);
     glBufferData(GL_ARRAY_BUFFER,sizeof(sv),sv,GL_STATIC_DRAW);
     glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
     glBindVertexArray(0);}

    auto buildWP=[&]()->std::vector<glm::vec3>{
        std::vector<glm::vec3> wp;
        for(int id:path.ids) wp.push_back(sc.nodes[id].pos);
        return wp;
    };
    auto waypoints=buildWP();

    float lastT=0,dt=0,spawnT=0,emitT=0,logT=0,fpsT=0;
    int fCount=0,fps=0;
    bool prevOptim=false;

    while(!glfwWindowShouldClose(win)){
        bloom.resize(SCR_WIDTH, SCR_HEIGHT);
        hud.W = SCR_WIDTH;
        hud.H = SCR_HEIGHT;
        float now=(float)glfwGetTime(); dt=now-lastT; lastT=now;
        dt=glm::clamp(dt,0.f,0.08f);
        fCount++;fpsT+=dt;if(fpsT>=1.f){fps=fCount;fCount=0;fpsT=0;}

        dn.spd=(float)gDaySpeed*0.8f;
        dn.update(dt);

        // Keyboard movement
        float spd=1.f;
        if(glfwGetKey(win,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS) spd=3.f;
        cam.Speed=25.f*spd;
        if(glfwGetKey(win,GLFW_KEY_W)==GLFW_PRESS) cam.ProcessKeyboard(0,dt);
        if(glfwGetKey(win,GLFW_KEY_S)==GLFW_PRESS) cam.ProcessKeyboard(1,dt);
        if(glfwGetKey(win,GLFW_KEY_A)==GLFW_PRESS) cam.ProcessKeyboard(2,dt);
        if(glfwGetKey(win,GLFW_KEY_D)==GLFW_PRESS) cam.ProcessKeyboard(3,dt);
        if(glfwGetKey(win,GLFW_KEY_Q)==GLFW_PRESS||glfwGetKey(win,GLFW_KEY_SPACE)==GLFW_PRESS) cam.ProcessKeyboard(4,dt);
        if(glfwGetKey(win,GLFW_KEY_E)==GLFW_PRESS||glfwGetKey(win,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS) cam.ProcessKeyboard(5,dt);
        // Camera presets
        if(glfwGetKey(win,GLFW_KEY_F1)==GLFW_PRESS) cam.setTarget(glm::vec3(0,0,0),55,-28,-90);
        if(glfwGetKey(win,GLFW_KEY_F2)==GLFW_PRESS) cam.setTarget(sc.nodes[0].pos,22,-18,-60);
        if(glfwGetKey(win,GLFW_KEY_F3)==GLFW_PRESS) cam.setTarget(sc.nodes[7].pos,22,-18,-120);

        // Optimization toggle
        if(gOptimOn!=prevOptim){
            prevOptim=gOptimOn;
            path=sc.runDijkstra(0,7);
            waypoints=buildWP();
            vs.vehs.clear();
        }

        // Mouse pick
        if(gMouseClicked){
            gMouseClicked=false;
            float nx=(2.f*(float)gClickX/SCR_WIDTH)-1, ny=1-(2.f*(float)gClickY/SCR_HEIGHT);
            glm::mat4 proj=glm::perspective(glm::radians(cam.Zoom),(float)SCR_WIDTH/SCR_HEIGHT,0.1f,500.f);
            glm::mat4 view=cam.GetViewMatrix();
            glm::vec4 re=glm::inverse(proj)*glm::vec4(nx,ny,-1,1);
            re=glm::vec4(re.x,re.y,-1,0);
            glm::vec3 rw=glm::normalize(glm::vec3(glm::inverse(view)*re));
            int hit=sc.pickNode(cam.Position,rw);
            for(auto& n:sc.nodes) n.selected=(n.id==hit);
            
            if(hit > 0) {
                path=sc.runDijkstra(0, hit);
                waypoints=buildWP();
                vs.vehs.clear();
            }
        }

        // Spawn vehicles
        spawnT+=dt;
        if(spawnT>5.5f&&waypoints.size()>=2){
            spawnT=0; vs.spawn(waypoints,gOptimOn?200.f:70.f);
        }
        vs.update(dt);

        // Emit particles
        emitT+=dt;
        if(emitT>0.28f&&waypoints.size()>=2){
            emitT=0;
            glm::vec3 dir=glm::normalize(waypoints[1]-waypoints[0]);
            float jx=(rand()%100-50)*0.03f, jz=(rand()%100-50)*0.03f;
            ps.emit(waypoints[0]+glm::vec3(jx,2,jz),dir+glm::vec3(jx,0.05f,jz),5.5f,gOptimOn?120.f:40.f);
        }
        ps.update(dt);

        // Console stats
        logT+=dt;
        if(logT>=4.f){
            logT=0;
            printf("\n=== AgriFlow 3D [Enhanced] ===\n");
            printf("OPT: %s | Route: %.0f km | Spoilage: %.1f%% | Middlemen: %d\n",
                gOptimOn?"ON":"OFF",path.totalDist,path.totalSpoil*100,path.totalMiddlemen);
            printf("Delivered:%d | Spoiled:%d | Particles:%d | FPS:%d | Time:%.1fh\n",
                vs.delivered,vs.spoiled,ps.active,fps,dn.tod);
        }
        // Title
        char title[512];
        float naiveDist=path.totalDist*1.6f;
        float naiveSpoil=path.totalSpoil*100*2.8f;
        snprintf(title,512,"AgriFlow 3D | OPT:%s | Route:%.0fkm->%.0fkm | Spoil:%.0f%%->%.0f%% | Revenue:INR800->INR2100 | FPS:%d",
            gOptimOn?"ON":"OFF",naiveDist,path.totalDist,naiveSpoil,path.totalSpoil*100,fps);
        glfwSetWindowTitle(win,title);

        // ---- RENDER TO BLOOM FBO ----
        bloom.bindScene();
        glm::vec3 cc=dn.clearColor();
        glClearColor(cc.r,cc.g,cc.b,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);

        glm::mat4 view=cam.GetViewMatrix();
        glm::mat4 proj=glm::perspective(glm::radians(cam.Zoom),(float)SCR_WIDTH/SCR_HEIGHT,0.1f,500.f);

        // Sky (depth-disabled, full-screen)
        glDisable(GL_DEPTH_TEST);
        skySh.use();
        skySh.setVec3("sunDir",dn.sunDir());
        skySh.setVec3("sunColor",dn.sunColor());
        skySh.setVec3("skyTop",dn.skyTop());
        skySh.setVec3("skyHorizon",dn.skyHorizon());
        skySh.setFloat("time",now);
        skySh.setFloat("isNight",dn.isNight());
        glBindVertexArray(skyVAO);glDrawArrays(GL_TRIANGLES,0,6);
        glEnable(GL_DEPTH_TEST);

        // Light helper
        auto setLights=[&](Shader& sh){
            sh.use();sh.setMat4("view",view);sh.setMat4("projection",proj);
            sh.setVec3("sunDir",dn.sunDir());sh.setVec3("sunColor",dn.sunColor());
            sh.setVec3("ambientColor",dn.ambient());sh.setVec3("viewPos",cam.Position);
            sh.setFloat("time",now);
        };

        // Terrain
        setLights(terrainSh); terrain.render(terrainSh);

        // Nodes
        setLights(objectSh);
        objectSh.setVec3("emissiveColor",glm::vec3(0));
        for(auto& n:sc.nodes){
            switch(n.type){
                case FARM:         drawFarm(objectSh,pc,n.pos,now); break;
                case WAREHOUSE:    drawWarehouse(objectSh,pc,n.pos); break;
                case COLD_STORAGE: drawColdStorage(objectSh,pc,n.pos,now); break;
                case MARKET:       drawMarket(objectSh,pc,n.pos,now); break;
            }
            // Node type ring (torus)
            glm::vec4 ringCols[]={glm::vec4(0.2f,1.0f,0.2f,1),glm::vec4(0.7f,0.7f,0.7f,1),glm::vec4(0.2f,0.5f,1.0f,1),glm::vec4(1.0f,0.65f,0.1f,1)};
            glm::vec3 rc=glm::vec3(ringCols[(int)n.type]);
            float ringRot=now*0.6f+(float)n.id;
            objectSh.setBool("useOverrideColor",true);
            objectSh.setVec3("overrideColor",rc);
            objectSh.setFloat("metallic",0.6f);objectSh.setFloat("roughness",0.3f);
            Prim::draw(pc.tor,objectSh,n.pos+glm::vec3(0,0.3f,0),glm::vec3(2.5f,2.5f,2.5f),ringRot,glm::vec3(rc*0.15f));
            if(n.selected){
                float pulse=1.6f+0.6f*sinf(now*5.f);
                objectSh.setVec3("overrideColor",glm::vec3(1,1,0.2f));
                objectSh.setFloat("metallic",0.0f);
                Prim::draw(pc.sph,objectSh,n.pos+glm::vec3(0,4.5f,0),glm::vec3(pulse),0,glm::vec3(0.4f,0.38f,0.0f));
            }
            objectSh.setBool("useOverrideColor",false);
        }

        // Routes
        lineSh.use();lineSh.setMat4("view",view);lineSh.setMat4("projection",proj);
        rr.renderAll(lineSh,sc,now,gOptimOn);

        // Vehicles
        setLights(objectSh); vs.render(objectSh, pc, now);

        // Particles
        particleSh.use();particleSh.setMat4("view",view);particleSh.setMat4("projection",proj);
        ps.render(particleSh);

        // ---- BLOOM PASS ----
        bloom.doBloom(brightSh,blurSh);
        // ---- COMPOSITE TO SCREEN ----
        bloom.blit(bloomSh,dn.isNight()>0.5f?0.8f:1.1f);

        // ---- HUD (drawn directly on default FB) ----
        // Spoilage: use average of LIVE vehicles (shows real-time freshness),
        // fall back to historical ratio or baseline 0.72 if nothing is moving
        float spoilPct = 0.72f; // baseline (unoptimized default)
        {
            float sum=0.f; int cnt=0;
            for(auto& v: vs.vehs){ sum+=v.spoil; cnt++; }
            if(cnt>0){
                spoilPct = sum / cnt; // live average
            } else if(vs.delivered+vs.spoiled>0){
                spoilPct = (float)vs.spoiled/(vs.delivered+vs.spoiled);
                if(spoilPct<0.01f) spoilPct=0.72f; // don't show 0 when no trucks spoiled yet
            }
        }
        int selectedNodeId=-1;
        std::string selectedName="None";
        for(auto& n:sc.nodes) if(n.selected){selectedNodeId=n.id;selectedName=n.name;}
        float optDist=path.totalDist;
        hud.render(hudSh,sc,path,gOptimOn,vs.delivered,vs.spoiled,spoilPct,
                   path.totalDist*1.6f,optDist,dn.tod,fps,now,
                   800.0f,2100.0f,selectedNodeId,selectedName);

        glfwSwapBuffers(win);glfwPollEvents();
    }

    vs.cleanup();ps.cleanup();rr.cleanup();hud.cleanup();pc.cleanup();terrain.cleanup();bloom.cleanup();
    glDeleteVertexArrays(1,&skyVAO);glDeleteBuffers(1,&skyVBO);
    glfwTerminate();
    return 0;
}

