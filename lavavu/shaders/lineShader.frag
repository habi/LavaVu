varying vec4 vColour;
varying vec3 vVertex;
uniform float uOpacity;
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform vec3 uClipMin;
uniform vec3 uClipMax;
uniform bool uOpaque;

void main(void)
{
  //Clip planes in X/Y/Z (shift seems to be required on nvidia)
  if (any(lessThan(vVertex, uClipMin - vec3(0.01))) || any(greaterThan(vVertex, uClipMax + vec3(0.01)))) discard;

  vec4 colour = vColour;
  float alpha = colour.a;
  if (uOpacity > 0.0) alpha *= uOpacity;

  //Brightness adjust
  colour += uBrightness;
  //Saturation & Contrast adjust
  const vec4 LumCoeff = vec4(0.2125, 0.7154, 0.0721, 0.0);
  vec4 AvgLumin = vec4(0.5, 0.5, 0.5, 0.0);
  vec4 intensity = vec4(dot(colour, LumCoeff));
  colour = mix(intensity, colour, uSaturation);
  colour = mix(AvgLumin, colour, uContrast);
  colour.a = alpha;

  if (alpha < 0.01) discard;

  if (uOpaque)
    colour.a = 1.0;

  gl_FragColor = colour;
}
