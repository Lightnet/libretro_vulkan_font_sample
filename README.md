# libretro_vulkan_font_sample

# License: MIT

# Information:
  Sample vulkan text render. By using the stb_truetype.h.

  Simple hello world center of the screen.

# retroarch:
- cores folder should be place with liblibretro_vulkan.dll.
- system folder place font there.

# Notes:
- stb_truetype typically returns quads with Y coordinates increasing upward while Vulkan’s viewport has Y increasing downward. This can cause the text to appear upside-down.
- In init_text_vertex_buffer, modify the vertex position calculations to normalize the coordinates or map them to screen space. Since the orthographic projection already maps (0,0) to the top-left and (width,height) to the bottom-right, we can keep the positions in pixel space but adjust the centering logic.
- The vertex positions from stbtt_GetBakedQuad are in pixel coordinates relative to the font atlas. To work with Vulkan’s orthographic projection, normalize these coordinates to the range [0, 1] or map them directly to screen space.

# Adjust the Scaling Factor:
  The scale factor in init_text_vertex_buffer should account for the font size relative to the screen resolution. Instead of a fixed scale = 1.0f, calculate it based on the font size and atlas dimensions, and adjust it to fit the screen.

```
float scale = FONT_SIZE / ATLAS_HEIGHT; // Base scale relative to atlas
// Adjust scale to fit within screen dimensions
scale *= (float)height / FONT_SIZE; // Scale to match screen height
```
This ensures the text size is proportional to the screen height. You can further adjust the scale to make the text larger or smaller as needed.



# center text:

```
float text_width = max_x - min_x;
float text_height = max_y - min_y;
float start_x = (width - text_width * scale) * 0.5f; // Center horizontally
float start_y = (height - text_height * scale) * 0.5f; // Center vertically
```


## Modify the vertex positions to flip Y:
```
vertices[vtx_count + 0].pos[1] = (q.y0 * scale - start_y); // Flip Y
vertices[vtx_count + 1].pos[1] = (q.y0 * scale - start_y);
vertices[vtx_count + 2].pos[1] = (q.y1 * scale - start_y);
vertices[vtx_count + 3].pos[1] = (q.y1 * scale - start_y);
```

Alternatively, flip the texture coordinates in the vertex shader or adjust the atlas during initialization. To flip the texture vertically, modify the t0 and t1 coordinates:
```
vertices[vtx_count + 0].tex[1] = 1.0f - q.t0; // Flip t0
vertices[vtx_count + 1].tex[1] = 1.0f - q.t0;
vertices[vtx_count + 2].tex[1] = 1.0f - q.t1;
vertices[vtx_count + 3].tex[1] = 1.0f - q.t1;
```
## Orthographic Projection:
The update_ubo function creates an orthographic projection matrix using glm_ortho. Ensure it correctly maps the vertex positions to the screen. The current setup (0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f) is correct for Vulkan’s coordinate system, but verify that the vertex positions are in the expected range.


## Fragment Shader for Debugging:
```glsl
#version 310 es
precision mediump float;
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1) uniform sampler2D FontTexture;

void main() {
    //float alpha = texture(FontTexture, vTexCoord).r;
    //FragColor = vec4(vColor.rgb, vColor.a * alpha);
    FragColor = vec4(1.0, 0.0, 0.0, 1.0); // Red for debugging
}
```
If red quads appear centered on the screen, the vertex positions are correct, and the issue lies in the texture sampling or atlas.

# Inspiration from the Reference
The provided reference (https://dev.to/shreyaspranav/how-to-render-truetype-fonts-in-opengl-using-stbtruetypeh-1p5k) suggests the following for OpenGL, which we can adapt for Vulkan:

- Orthographic Projection: Use an orthographic projection to map pixel coordinates to screen space, which is already implemented.
- Text Centering: Calculate the text bounding box and offset the quads to center them, which is partially implemented but needs the scaling and Y-flip fixes.
- Texture Atlas: Ensure the atlas is correctly uploaded and sampled, which appears correct but needs verification.
- Blending: The Vulkan pipeline already enables blending with VK_BLEND_FACTOR_SRC_ALPHA, which is consistent with the reference.

# Credits:
- https://dev.to/shreyaspranav/how-to-render-truetype-fonts-in-opengl-using-stbtruetypeh-1p5k
- Grok https://x.com/i/grok
- https://kenney.nl/assets/kenney-fonts