export const VS = `#version 300 es
out vec2 vUv;
void main() {
    vec2 uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = uv;
    gl_Position = vec4(uv * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
`;

const GATHER_GLSL =  `

ivec2 gatherBase(sampler2D t, vec2 p) {
    return ivec2(floor(p * vec2(textureSize(t, 0)) - 0.5));
}
vec3 fetchClamped(sampler2D t, ivec2 c) {
    ivec2 mx = textureSize(t, 0) - 1;
    return texelFetch(t, clamp(c, ivec2(0), mx), 0).rgb;
}

void gather3(sampler2D t, vec2 p, out vec4 r, out vec4 g, out vec4 b) {
    ivec2 c = gatherBase(t, p);
    vec3 t00 = fetchClamped(t, c);
    vec3 t10 = fetchClamped(t, c + ivec2(1, 0));
    vec3 t01 = fetchClamped(t, c + ivec2(0, 1));
    vec3 t11 = fetchClamped(t, c + ivec2(1, 1));
    r = vec4(t01.r, t11.r, t10.r, t00.r);
    g = vec4(t01.g, t11.g, t10.g, t00.g);
    b = vec4(t01.b, t11.b, t10.b, t00.b);
}
vec4 gatherG(sampler2D t, vec2 p) {
    ivec2 c = gatherBase(t, p);
    return vec4(fetchClamped(t, c + ivec2(0, 1)).g, fetchClamped(t, c + ivec2(1, 1)).g,
                fetchClamped(t, c + ivec2(1, 0)).g, fetchClamped(t, c).g);
}
`;





export const EASU_FS =
    `#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec4 uRes;
in vec2 vUv;
out vec4 oColor;
` +
    GATHER_GLSL +
     `
void easuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp,
             vec3 c) {
    vec2 v;
    v.x = off.x * dir.x + off.y * dir.y;
    v.y = off.x * (-dir.y) + off.y * dir.x;
    v *= len;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wB = 2.0 / 5.0 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}
void easuSet(inout vec2 dir, inout float len, vec2 pp, bool biS, bool biT, bool biU, bool biV,
             float lA, float lB, float lC, float lD, float lE) {
    float w = 0.0;
    if (biS) w = (1.0 - pp.x) * (1.0 - pp.y);
    else if (biT) w = pp.x * (1.0 - pp.y);
    else if (biU) w = (1.0 - pp.x) * pp.y;
    else if (biV) w = pp.x * pp.y;
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = 1.0 / (lenX + 1e-6);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;
    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = 1.0 / (lenY + 1e-6);
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}
void main() {
    vec2 inputSize = uRes.xy;
    vec2 outputSize = uRes.zw;
    vec2 pp = (floor(vUv * outputSize) + 0.5) / outputSize * inputSize - 0.5;
    vec2 fp = floor(pp);
    pp -= fp;
    vec2 p0 = fp + vec2(1.0, -1.0);
    vec2 p1 = p0 + vec2(-1.0, 2.0);
    vec2 p2 = p0 + vec2(1.0, 2.0);
    vec2 p3 = p0 + vec2(0.0, 4.0);
    p0 /= inputSize;
    p1 /= inputSize;
    p2 /= inputSize;
    p3 /= inputSize;
    vec4 bczzR, bczzG, bczzB, ijfeR, ijfeG, ijfeB, klhgR, klhgG, klhgB, zzonR, zzonG, zzonB;
    gather3(uTex, p0, bczzR, bczzG, bczzB);
    gather3(uTex, p1, ijfeR, ijfeG, ijfeB);
    gather3(uTex, p2, klhgR, klhgG, klhgB);
    gather3(uTex, p3, zzonR, zzonG, zzonB);
    vec4 bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);
    vec4 ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);
    vec4 klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);
    vec4 zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);
    float bL = bczzL.x, cL = bczzL.y;
    float iL = ijfeL.x, jL = ijfeL.y, fL = ijfeL.z, eL = ijfeL.w;
    float kL = klhgL.x, lL = klhgL.y, hL = klhgL.z, gL = klhgL.w;
    float oL = zzonL.z, nL = zzonL.w;
    vec2 dir = vec2(0.0);
    float len = 0.0;
    easuSet(dir, len, pp, true, false, false, false, bL, eL, fL, gL, jL);
    easuSet(dir, len, pp, false, true, false, false, cL, fL, gL, hL, kL);
    easuSet(dir, len, pp, false, false, true, false, fL, iL, jL, kL, nL);
    easuSet(dir, len, pp, false, false, false, true, gL, jL, kL, lL, oL);
    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < 1.0 / 32768.0;
    dirR = 1.0 / sqrt(dirR);
    if (zro) { dirR = 1.0; dir.x = 1.0; }
    dir *= dirR;
    len *= 0.5;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) * (1.0 / max(abs(dir.x), abs(dir.y)));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;
    vec3 min4 = min(min(vec3(ijfeR.z, ijfeG.z, ijfeB.z), min(vec3(klhgR.w, klhgG.w, klhgB.w),
                    vec3(ijfeR.y, ijfeG.y, ijfeB.y))), vec3(klhgR.x, klhgG.x, klhgB.x));
    vec3 max4 = max(max(vec3(ijfeR.z, ijfeG.z, ijfeB.z), max(vec3(klhgR.w, klhgG.w, klhgB.w),
                    vec3(ijfeR.y, ijfeG.y, ijfeB.y))), vec3(klhgR.x, klhgG.x, klhgB.x));
    vec3 aC = vec3(0.0);
    float aW = 0.0;
    easuTap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.x, bczzG.x, bczzB.x));
    easuTap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.y, bczzG.y, bczzB.y));
    easuTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.x, ijfeG.x, ijfeB.x));
    easuTap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.y, ijfeG.y, ijfeB.y));
    easuTap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.z, ijfeG.z, ijfeB.z));
    easuTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.w, ijfeG.w, ijfeB.w));
    easuTap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.x, klhgG.x, klhgB.x));
    easuTap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.y, klhgG.y, klhgB.y));
    easuTap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.z, klhgG.z, klhgB.z));
    easuTap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.w, klhgG.w, klhgB.w));
    easuTap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.z, zzonG.z, zzonB.z));
    easuTap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.w, zzonG.w, zzonB.w));
    vec3 c = min(max4, max(min4, aC * (1.0 / aW)));
    oColor = vec4(c, 1.0);
}
`;






export const RCAS_FS = `#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec4 uRes;
in vec2 vUv;
out vec4 oColor;
uniform float uSharpness;
uniform float uFlip;
float min3f(float a, float b, float c) { return min(a, min(b, c)); }
float max3f(float a, float b, float c) { return max(a, max(b, c)); }
void main() {
    vec2 uv = vec2(vUv.x, mix(vUv.y, 1.0 - vUv.y, uFlip));
    const float FSR_RCAS_LIMIT = 0.25 - (1.0 / 16.0);
    vec3 b = texture(uTex, uv + vec2(0.0, -uRes.w)).rgb;
    vec3 d = texture(uTex, uv + vec2(-uRes.z, 0.0)).rgb;
    vec3 e = texture(uTex, uv).rgb;
    vec3 f = texture(uTex, uv + vec2(uRes.z, 0.0)).rgb;
    vec3 h = texture(uTex, uv + vec2(0.0, uRes.w)).rgb;
    float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);
    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
    nz = clamp(abs(nz) * (1.0 / (max(1e-6, max3f(max3f(bL, dL, eL), fL, hL) - min3f(min3f(bL, dL, eL), fL, hL)))),
               0.0, 1.0);
    nz = -0.5 * nz + 1.0;
    vec3 mn4 = min(min(b, d), min(f, h));
    vec3 mx4 = max(max(b, d), max(f, h));
    vec2 peakC = vec2(1.0, -1.0 * 4.0);
    vec3 hitMin = min(mn4, e) * (1.0 / max(vec3(1e-6), 4.0 * mx4));
    vec3 hitMax = (peakC.x - max(mx4, e)) * (1.0 / min(vec3(-1e-6), 4.0 * mn4 + peakC.y));
    vec3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-FSR_RCAS_LIMIT, min(max3f(lobeRGB.r, lobeRGB.g, lobeRGB.b), 0.0)) * uSharpness;
    lobe *= nz;
    float rcpL = 1.0 / (4.0 * lobe + 1.0);
    vec3 c = (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;
    oColor = vec4(c, 1.0);
}
`;


export const SGSR_FS =
    `#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec4 uView;
in vec2 vUv;
out vec4 oColor;
` +
    GATHER_GLSL +
     `
const float EDGE_THRESHOLD = 6.0 / 255.0;
const float EDGE_SHARPNESS = 1.5;
float fastLanczos2(float x) {
    float wA = x - 4.0;
    float wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}
vec2 weightY(float dx, float dy, float c, vec3 data) {
    float stdev = data.x;
    vec2 dir = data.yz;
    float edgeDis = (dx * dir.y) + (dy * dir.x);
    float x = ((dx * dx) + (dy * dy)) +
              ((edgeDis * edgeDis) * ((clamp((c * c) * stdev, 0.0, 1.0) * 0.7) - 1.0));
    float w = fastLanczos2(x);
    return vec2(w, w * c);
}
vec2 edgeDirection(vec4 left, vec4 right) {
    float RxLz = right.x - left.z;
    float RwLy = right.w - left.y;
    vec2 delta = vec2(RxLz + RwLy, RxLz - RwLy);
    float lengthInv = inversesqrt((delta.x * delta.x + 3.075740e-05) + (delta.y * delta.y));
    return delta * lengthInv;
}
void main() {
    vec2 uv = vUv;
    vec4 con1 = uView;
    vec4 pix = vec4(texture(uTex, uv).rgb, 1.0);
    vec2 imgCoord = (uv * con1.zw) + vec2(-0.5, 0.5);
    vec2 imgCoordPixel = floor(imgCoord);
    vec2 coord = imgCoordPixel * con1.xy;
    vec2 pl = imgCoord - imgCoordPixel;
    vec4 left = gatherG(uTex, coord);
    float pixL = pix.y;
    float edgeVote = abs(left.z - left.y) + abs(pixL - left.y) + abs(pixL - left.z);
    if (edgeVote > EDGE_THRESHOLD) {
        coord.x += con1.x;
        vec4 right = gatherG(uTex, coord + vec2(con1.x, 0.0));
        vec4 udUp = gatherG(uTex, coord + vec2(0.0, -con1.y));
        vec4 udDn = gatherG(uTex, coord + vec2(0.0, con1.y));
        vec4 upDown = vec4(udUp.w, udUp.z, udDn.y, udDn.x);
        float mean = (left.y + left.z + right.x + right.w) * 0.25;
        left -= vec4(mean);
        right -= vec4(mean);
        upDown -= vec4(mean);
        float pixW = pixL - mean;
        float sum = (abs(left.x) + abs(left.y) + abs(left.z) + abs(left.w)) +
                    (abs(right.x) + abs(right.y) + abs(right.z) + abs(right.w)) +
                    (abs(upDown.x) + abs(upDown.y) + abs(upDown.z) + abs(upDown.w));
        float sumMean = 1.014185e+01 / sum;
        float stdev = sumMean * sumMean;
        vec3 data = vec3(stdev, edgeDirection(left, right));
        vec2 aWY = weightY(pl.x, pl.y + 1.0, upDown.x, data);
        aWY += weightY(pl.x - 1.0, pl.y + 1.0, upDown.y, data);
        aWY += weightY(pl.x - 1.0, pl.y - 2.0, upDown.z, data);
        aWY += weightY(pl.x, pl.y - 2.0, upDown.w, data);
        aWY += weightY(pl.x + 1.0, pl.y - 1.0, left.x, data);
        aWY += weightY(pl.x, pl.y - 1.0, left.y, data);
        aWY += weightY(pl.x, pl.y, left.z, data);
        aWY += weightY(pl.x + 1.0, pl.y, left.w, data);
        aWY += weightY(pl.x - 1.0, pl.y - 1.0, right.x, data);
        aWY += weightY(pl.x - 2.0, pl.y - 1.0, right.y, data);
        aWY += weightY(pl.x - 2.0, pl.y, right.z, data);
        aWY += weightY(pl.x - 1.0, pl.y, right.w, data);
        float finalY = aWY.y / aWY.x;
        float max4 = max(max(left.y, left.z), max(right.x, right.w));
        float min4 = min(min(left.y, left.z), min(right.x, right.w));
        finalY = clamp(EDGE_SHARPNESS * finalY, min4, max4);
        float deltaY = finalY - pixW;
        pix.rgb = clamp(pix.rgb + vec3(deltaY), 0.0, 1.0);
    }
    pix.w = 1.0;
    oColor = pix;
}
`;
