#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec2 vUV;
layout(location=0) out vec4 fragColor;
layout(set=2, binding=0) uniform sampler2D diffuseTex;
layout(set=3, binding=0) uniform FragUBO { vec4 baseColor; vec4 lightDir; vec4 ambient; };
void main() {
    vec3 n = normalize(vNormal);
    float ndl = max(dot(n, normalize(-lightDir.xyz)), 0.0);
    vec4 tex = texture(diffuseTex, vUV);
    vec3 lit = ambient.rgb + ndl * (1.0 - ambient.rgb);
    fragColor = vec4(tex.rgb * baseColor.rgb * lit, tex.a * baseColor.a);
}