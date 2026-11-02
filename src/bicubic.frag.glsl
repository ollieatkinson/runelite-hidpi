// Two separable passes using Pixman's convolution weights and 16 subpixel phases.
// Keep negative lobes in RGBA16F until the final (vertical) pass.
uniform sampler2D image;
uniform float sourceScale;
uniform int axis;
uniform int taps;
uniform float coefficients[256];
out vec4 color;

void main()
{
    ivec2 dims = textureSize(image, 0);
    float position = gl_FragCoord[axis] * sourceScale;
    float rounded = (floor(position * 16.0) + 0.5) / 16.0;
    int phase = int(fract(rounded) * 16.0);
    int start = int(floor(rounded - 1.0 / 65536.0 - float(taps - 1) * 0.5));
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 sum = vec4(0);

    for (int i = 0; i < 16; i++) {
        if (i >= taps)
            break;
        pixel[axis] = start + i;
        sum += texelFetch(image, clamp(pixel, ivec2(0), dims - 1), 0)
             * coefficients[phase * taps + i];
    }
    color = axis == 0 ? sum : clamp(sum, 0.0, 1.0);
}
