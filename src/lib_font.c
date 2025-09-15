// https://github.com/libretro/libretro-samples/tree/master/video/vulkan/vk_rendering
// working

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "triangle_frag.h"
#include "triangle_vert.h"

#include "vulkan/vulkan_symbol_wrapper.h"
#include "libretro_vulkan.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <cglm/cglm.h>

static struct retro_hw_render_callback hw_render;
static const struct retro_hw_render_interface_vulkan *vulkan;

#define BASE_WIDTH 320
#define BASE_HEIGHT 240
#define MAX_SYNC 8

#define FONT_SIZE 32.0f
#define ATLAS_WIDTH 512
#define ATLAS_HEIGHT 512

static unsigned width  = BASE_WIDTH;
static unsigned height = BASE_HEIGHT;

struct buffer
{
   VkBuffer buffer;
   VkDeviceMemory memory;
};

struct vulkan_data
{
   unsigned index;
   unsigned num_swapchain_images;
   uint32_t swapchain_mask;
   struct buffer vbo;
   struct buffer ibo;
   struct buffer ubo[MAX_SYNC];

   VkPhysicalDeviceMemoryProperties memory_properties;
   VkPhysicalDeviceProperties gpu_properties;

   VkDescriptorSetLayout set_layout;
   VkDescriptorPool desc_pool;
   VkDescriptorSet desc_set[MAX_SYNC];

   VkPipelineCache pipeline_cache;
   VkPipelineLayout pipeline_layout;
   VkRenderPass render_pass;
   VkPipeline pipeline;

   struct retro_vulkan_image images[MAX_SYNC];
   VkDeviceMemory image_memory[MAX_SYNC];
   VkFramebuffer framebuffers[MAX_SYNC];
   VkCommandPool cmd_pool[MAX_SYNC];
   VkCommandBuffer cmd[MAX_SYNC];
};
static struct vulkan_data vk;

struct font_data {
    stbtt_bakedchar cdata[96]; // ASCII 32..126
    unsigned char* atlas; // Texture atlas data
    VkImage atlas_image;
    VkImageView atlas_image_view;
    VkDeviceMemory atlas_memory;
    VkSampler sampler;
};

static struct font_data font;

struct vertex {
    float pos[2];
    float tex[2];
    float color[4];
};


void retro_init(void)
{}

void retro_deinit(void)
{}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "TestCore Vulkan Sample";
   info->library_version  = "v1";
   info->need_fullpath    = false;
   info->valid_extensions = NULL; // Anything is fine, we don't care.
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->timing = (struct retro_system_timing) {
      .fps = 60.0,
      .sample_rate = 0.0,
   };

   info->geometry = (struct retro_game_geometry) {
      .base_width   = BASE_WIDTH,
      .base_height  = BASE_HEIGHT,
      .max_width    = BASE_WIDTH,
      .max_height   = BASE_HEIGHT,
      .aspect_ratio = (float)BASE_WIDTH / (float)BASE_HEIGHT,
   };
}

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_variable variables[] = {
      {
         "testvulkan_resolution",
         "Internal resolution; 320x240|360x480|480x272|512x384|512x512|640x240|640x448|640x480|720x576|800x600|960x720|1024x768|1024x1024|1280x720|1280x960|1600x1200|1920x1080|1920x1440|1920x1600|2048x2048",
      },
      { NULL, NULL },
   };

   bool no_rom = true;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_rom);
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

static uint32_t find_memory_type_from_requirements(
      uint32_t device_requirements, uint32_t host_requirements)
{
   const VkPhysicalDeviceMemoryProperties *props = &vk.memory_properties;
   for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
   {
      if (device_requirements & (1u << i))
      {
         if ((props->memoryTypes[i].propertyFlags & host_requirements) == host_requirements)
         {
            return i;
         }
      }
   }

   return 0;
}

// static void update_ubo(void)
// {
//    static unsigned frame;
//    float c = cosf(frame * 0.01f);
//    float s = sinf(frame * 0.01f);
//    frame++;
//    float tmp[16] = {0.0f};
//    tmp[ 0] = c;
//    tmp[ 1] = s;
//    tmp[ 4] = -s;
//    tmp[ 5] = c;
//    tmp[10] = 1.0f;
//    tmp[15] = 1.0f;
//    float *mvp = NULL;
//    vkMapMemory(vulkan->device, vk.ubo[vk.index].memory,
//          0, 16 * sizeof(float), 0, (void**)&mvp);
//    memcpy(mvp, tmp, sizeof(tmp));
//    vkUnmapMemory(vulkan->device, vk.ubo[vk.index].memory);
// }

// static void update_ubo(void)
// {
//    float tmp[16] = {
//       1.0f, 0.0f, 0.0f, 0.0f, // Row 1: No rotation, no scaling
//       0.0f, 1.0f, 0.0f, 0.0f, // Row 2
//       0.0f, 0.0f, 1.0f, 0.0f, // Row 3
//       0.0f, 0.0f, 0.0f, 1.0f  // Row 4
//    };

//    float *mvp = NULL;
//    vkMapMemory(vulkan->device, vk.ubo[vk.index].memory,
//          0, 16 * sizeof(float), 0, (void**)&mvp);
//    memcpy(mvp, tmp, sizeof(tmp));
//    vkUnmapMemory(vulkan->device, vk.ubo[vk.index].memory);
// }


// static void update_ubo(void) //white block.
// {
//    float tmp[16] = {
//       1.0f, 0.0f, 0.0f, 0.0f,
//       0.0f, 1.0f, 0.0f, 0.0f,
//       0.0f, 0.0f, 1.0f, 0.0f,
//       0.0f, 0.0f, 0.0f, 1.0f
//    };

//    float *mvp = NULL;
//    vkMapMemory(vulkan->device, vk.ubo[vk.index].memory,
//          0, 16 * sizeof(float), 0, (void**)&mvp);
//    memcpy(mvp, tmp, 16 * sizeof(float));
//    vkUnmapMemory(vulkan->device, vk.ubo[vk.index].memory);
// }

// static void update_ubo(void)
// {
//    mat4 ortho;
//    glm_ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f, ortho);

//    float *mvp = NULL;
//    vkMapMemory(vulkan->device, vk.ubo[vk.index].memory,
//          0, 16 * sizeof(float), 0, (void**)&mvp);
//    memcpy(mvp, ortho, 16 * sizeof(float));
//    vkUnmapMemory(vulkan->device, vk.ubo[vk.index].memory);
// }


static void update_ubo(void)
{
   mat4 ortho;
   glm_ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f, ortho);

   float *mvp = NULL;
   vkMapMemory(vulkan->device, vk.ubo[vk.index].memory,
         0, 16 * sizeof(float), 0, (void**)&mvp);
   memcpy(mvp, ortho, 16 * sizeof(float));
   vkUnmapMemory(vulkan->device, vk.ubo[vk.index].memory);
}



static void vulkan_test_render(void)
{
   update_ubo();
   VkCommandBuffer cmd = vk.cmd[vk.index];
   VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
   begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   vkResetCommandBuffer(cmd, 0);
   vkBeginCommandBuffer(cmd, &begin_info);

   VkImageMemoryBarrier prepare_rendering = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   prepare_rendering.srcAccessMask = 0;
   prepare_rendering.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
   prepare_rendering.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   prepare_rendering.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   prepare_rendering.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   prepare_rendering.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   prepare_rendering.image = vk.images[vk.index].create_info.image;
   prepare_rendering.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   prepare_rendering.subresourceRange.levelCount = 1;
   prepare_rendering.subresourceRange.layerCount = 1;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, false, 0, NULL, 0, NULL, 1, &prepare_rendering);

   VkClearValue clear_value;
   clear_value.color.float32[0] = 0.0f;
   clear_value.color.float32[1] = 0.0f;
   clear_value.color.float32[2] = 0.0f;
   clear_value.color.float32[3] = 1.0f;

   VkRenderPassBeginInfo rp_begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
   rp_begin.renderPass = vk.render_pass;
   rp_begin.framebuffer = vk.framebuffers[vk.index];
   rp_begin.renderArea.extent.width = width;
   rp_begin.renderArea.extent.height = height;
   rp_begin.clearValueCount = 1;
   rp_begin.pClearValues = &clear_value;
   vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

   vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);
   vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0, 1, &vk.desc_set[vk.index], 0, NULL);

   VkViewport vp = {0};
   vp.width = (float)width;
   vp.height = (float)height;
   vp.minDepth = 0.0f;
   vp.maxDepth = 1.0f;
   vkCmdSetViewport(cmd, 0, 1, &vp);

   VkRect2D scissor = {0};
   scissor.extent.width = width;
   scissor.extent.height = height;
   vkCmdSetScissor(cmd, 0, 1, &scissor);

   VkDeviceSize offset = 0;
   vkCmdBindVertexBuffers(cmd, 0, 1, &vk.vbo.buffer, &offset);
   vkCmdBindIndexBuffer(cmd, vk.ibo.buffer, 0, VK_INDEX_TYPE_UINT32);
   vkCmdDrawIndexed(cmd, strlen("Hello World") * 6, 1, 0, 0, 0);

   vkCmdEndRenderPass(cmd);

   VkImageMemoryBarrier prepare_presentation = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   prepare_presentation.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   prepare_presentation.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   prepare_presentation.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   prepare_presentation.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   prepare_presentation.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   prepare_presentation.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
   prepare_presentation.image = vk.images[vk.index].create_info.image;
   prepare_presentation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   prepare_presentation.subresourceRange.levelCount = 1;
   prepare_presentation.subresourceRange.layerCount = 1;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, false, 0, NULL, 0, NULL, 1, &prepare_presentation);

   vkEndCommandBuffer(cmd);
}



//===============================================
// 
//===============================================
static struct buffer create_buffer(const void *initial, size_t size, VkBufferUsageFlags usage)
{
   struct buffer buffer;
   VkDevice device = vulkan->device;

   VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
   info.usage = usage;
   info.size = size;

   vkCreateBuffer(device, &info, NULL, &buffer.buffer);

   VkMemoryRequirements mem_reqs;
   vkGetBufferMemoryRequirements(device, buffer.buffer, &mem_reqs);

   VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
   alloc.allocationSize = mem_reqs.size;

   alloc.memoryTypeIndex = find_memory_type_from_requirements(mem_reqs.memoryTypeBits,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

   vkAllocateMemory(device, &alloc, NULL, &buffer.memory);
   vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0);

   if (initial)
   {
      void *ptr;
      vkMapMemory(device, buffer.memory, 0, size, 0, &ptr);
      memcpy(ptr, initial, size);
      vkUnmapMemory(device, buffer.memory);
   }

   return buffer;
}


static void init_text_vertex_buffer(void)
{
   if (!font.cdata) {
      fprintf(stderr, "[libretro-test]: Font data not initialized, skipping vertex buffer creation\n");
      return;
   }

   const char* text = "Hello World";
   size_t len = strlen(text);
   struct vertex* vertices = malloc(len * 4 * sizeof(struct vertex));
   uint32_t* indices = malloc(len * 6 * sizeof(uint32_t));
   if (!vertices || !indices) {
      fprintf(stderr, "[libretro-test]: Failed to allocate vertex/index buffers\n");
      free(vertices);
      free(indices);
      return;
   }

   float scale = 1.0f; // Adjust as needed (e.g., FONT_SIZE / ATLAS_HEIGHT)
   float x = 0.0f, y = 0.0f;
   float min_x = FLT_MAX, max_x = -FLT_MAX;
   float min_y = FLT_MAX, max_y = -FLT_MAX;
   for (size_t i = 0; i < len; i++) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(font.cdata, ATLAS_WIDTH, ATLAS_HEIGHT, text[i] - 32, &x, &y, &q, 1);
      min_x = fmin(min_x, q.x0);
      max_x = fmax(max_x, q.x1);
      min_y = fmin(min_y, q.y0);
      max_y = fmax(max_y, q.y1);
   }

   float text_width = max_x - min_x;
   float text_height = max_y - min_y;
   float start_x = (width - text_width * scale) * 0.5f;
   float start_y = (height - text_height * scale) * 0.5f;

   size_t vtx_count = 0, idx_count = 0;
   x = start_x;
   y = start_y;
   for (size_t i = 0; i < len; i++) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(font.cdata, ATLAS_WIDTH, ATLAS_HEIGHT, text[i] - 32, &x, &y, &q, 1);

      fprintf(stderr, "[libretro-test]: Char %c: x0=%.1f, y0=%.1f, x1=%.1f, y1=%.1f, s0=%.3f, t0=%.3f, s1=%.3f, t1=%.3f\n",
              text[i], q.x0 * scale, q.y0 * scale, q.x1 * scale, q.y1 * scale, q.s0, q.t0, q.s1, q.t1);

      vertices[vtx_count + 0].pos[0] = q.x0 * scale;
      vertices[vtx_count + 0].pos[1] = q.y0 * scale;
      vertices[vtx_count + 0].tex[0] = q.s0;
      vertices[vtx_count + 0].tex[1] = q.t0;
      vertices[vtx_count + 0].color[0] = 1.0f;
      vertices[vtx_count + 0].color[1] = 1.0f;
      vertices[vtx_count + 0].color[2] = 1.0f;
      vertices[vtx_count + 0].color[3] = 1.0f;

      vertices[vtx_count + 1].pos[0] = q.x1 * scale;
      vertices[vtx_count + 1].pos[1] = q.y0 * scale;
      vertices[vtx_count + 1].tex[0] = q.s1;
      vertices[vtx_count + 1].tex[1] = q.t0;
      vertices[vtx_count + 1].color[0] = 1.0f;
      vertices[vtx_count + 1].color[1] = 1.0f;
      vertices[vtx_count + 1].color[2] = 1.0f;
      vertices[vtx_count + 1].color[3] = 1.0f;

      vertices[vtx_count + 2].pos[0] = q.x1 * scale;
      vertices[vtx_count + 2].pos[1] = q.y1 * scale;
      vertices[vtx_count + 2].tex[0] = q.s1;
      vertices[vtx_count + 2].tex[1] = q.t1;
      vertices[vtx_count + 2].color[0] = 1.0f;
      vertices[vtx_count + 2].color[1] = 1.0f;
      vertices[vtx_count + 2].color[2] = 1.0f;
      vertices[vtx_count + 2].color[3] = 1.0f;

      vertices[vtx_count + 3].pos[0] = q.x0 * scale;
      vertices[vtx_count + 3].pos[1] = q.y1 * scale;
      vertices[vtx_count + 3].tex[0] = q.s0;
      vertices[vtx_count + 3].tex[1] = q.t1;
      vertices[vtx_count + 3].color[0] = 1.0f;
      vertices[vtx_count + 3].color[1] = 1.0f;
      vertices[vtx_count + 3].color[2] = 1.0f;
      vertices[vtx_count + 3].color[3] = 1.0f;

      indices[idx_count + 0] = vtx_count + 0;
      indices[idx_count + 1] = vtx_count + 1;
      indices[idx_count + 2] = vtx_count + 2;
      indices[idx_count + 3] = vtx_count + 2;
      indices[idx_count + 4] = vtx_count + 3;
      indices[idx_count + 5] = vtx_count + 0;

      vtx_count += 4;
      idx_count += 6;
   }

   vk.vbo = create_buffer(vertices, vtx_count * sizeof(struct vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
   if (!vk.vbo.buffer || !vk.vbo.memory) {
      fprintf(stderr, "[libretro-test]: Failed to create vertex buffer\n");
      free(vertices);
      free(indices);
      return;
   }

   vk.ibo = create_buffer(indices, idx_count * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
   if (!vk.ibo.buffer || !vk.ibo.memory) {
      fprintf(stderr, "[libretro-test]: Failed to create index buffer\n");
      vkFreeMemory(vulkan->device, vk.vbo.memory, NULL);
      vkDestroyBuffer(vulkan->device, vk.vbo.buffer, NULL);
      free(vertices);
      free(indices);
      return;
   }

   free(vertices);
   free(indices);
   fprintf(stderr, "[libretro-test]: Vertex and index buffers initialized\n");
}


static void init_vertex_buffer(void)
{
   static const float data[] = {
      -0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, // vec4 position, vec4 color
      -0.5f, +0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
      +0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
   };

   vk.vbo = create_buffer(data, sizeof(data), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

static void init_uniform_buffer(void)
{
   for (unsigned i = 0; i < vk.num_swapchain_images; i++)
   {
      vk.ubo[i] = create_buffer(NULL, 16 * sizeof(float),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
   }
}

static VkShaderModule create_shader_module(const uint32_t *data, size_t size)
{
   VkShaderModuleCreateInfo module_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
   VkShaderModule module;
   module_info.codeSize = size;
   module_info.pCode = data;
   vkCreateShaderModule(vulkan->device, &module_info, NULL, &module);
   return module;
}



static void init_descriptor(void)
{
   VkDevice device = vulkan->device;

   VkDescriptorSetLayoutBinding bindings[2] = {{0}};
   bindings[0].binding = 0;
   bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
   bindings[0].descriptorCount = 1;
   bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

   bindings[1].binding = 1;
   bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
   bindings[1].descriptorCount = 1;
   bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
   bindings[1].pImmutableSamplers = &font.sampler;

   const VkDescriptorPoolSize pool_sizes[2] = {
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk.num_swapchain_images },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, vk.num_swapchain_images },
   };

   VkDescriptorSetLayoutCreateInfo set_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
   set_layout_info.bindingCount = 2;
   set_layout_info.pBindings = bindings;
   VkResult res = vkCreateDescriptorSetLayout(device, &set_layout_info, NULL, &vk.set_layout);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create descriptor set layout: %d\n", res);
      return;
   }

   VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
   layout_info.setLayoutCount = 1;
   layout_info.pSetLayouts = &vk.set_layout;
   res = vkCreatePipelineLayout(device, &layout_info, NULL, &vk.pipeline_layout);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create pipeline layout: %d\n", res);
      vkDestroyDescriptorSetLayout(device, vk.set_layout, NULL);
      return;
   }

   VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
   pool_info.maxSets = vk.num_swapchain_images;
   pool_info.poolSizeCount = 2;
   pool_info.pPoolSizes = pool_sizes;
   pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
   res = vkCreateDescriptorPool(device, &pool_info, NULL, &vk.desc_pool);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create descriptor pool: %d\n", res);
      vkDestroyPipelineLayout(device, vk.pipeline_layout, NULL);
      vkDestroyDescriptorSetLayout(device, vk.set_layout, NULL);
      return;
   }

   VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
   alloc_info.descriptorPool = vk.desc_pool;
   alloc_info.descriptorSetCount = 1;
   alloc_info.pSetLayouts = &vk.set_layout;
   for (unsigned i = 0; i < vk.num_swapchain_images; i++)
   {
      res = vkAllocateDescriptorSets(device, &alloc_info, &vk.desc_set[i]);
      if (res != VK_SUCCESS) {
         fprintf(stderr, "[libretro-test]: Failed to allocate descriptor set %u: %d\n", i, res);
         vkDestroyDescriptorPool(device, vk.desc_pool, NULL);
         vkDestroyPipelineLayout(device, vk.pipeline_layout, NULL);
         vkDestroyDescriptorSetLayout(device, vk.set_layout, NULL);
         return;
      }

      VkWriteDescriptorSet writes[2] = {{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }, { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }};
      VkDescriptorBufferInfo buffer_info = {0};
      VkDescriptorImageInfo image_info = {0};

      writes[0].dstSet = vk.desc_set[i];
      writes[0].dstBinding = 0;
      writes[0].descriptorCount = 1;
      writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writes[0].pBufferInfo = &buffer_info;
      buffer_info.buffer = vk.ubo[i].buffer;
      buffer_info.offset = 0;
      buffer_info.range = 16 * sizeof(float);

      writes[1].dstSet = vk.desc_set[i];
      writes[1].dstBinding = 1;
      writes[1].descriptorCount = 1;
      writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[1].pImageInfo = &image_info;
      image_info.sampler = font.sampler;
      image_info.imageView = font.atlas_image_view;
      image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      vkUpdateDescriptorSets(device, 2, writes, 0, NULL);
   }
   fprintf(stderr, "[libretro-test]: Descriptor sets initialized\n");
}



static void init_pipeline(void)
{
   VkDevice device = vulkan->device;

   VkShaderModule vert_module, frag_module;
   VkShaderModuleCreateInfo module_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
   module_info.codeSize = sizeof(triangle_vert);
   module_info.pCode = triangle_vert;
   VkResult res = vkCreateShaderModule(device, &module_info, NULL, &vert_module);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create vertex shader module: %d\n", res);
      return;
   }

   module_info.codeSize = sizeof(triangle_frag);
   module_info.pCode = triangle_frag;
   res = vkCreateShaderModule(device, &module_info, NULL, &frag_module);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create fragment shader module: %d\n", res);
      vkDestroyShaderModule(device, vert_module, NULL);
      return;
   }

   VkPipelineShaderStageCreateInfo stages[2] = {
      { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_module, "main", NULL },
      { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag_module, "main", NULL }
   };

   VkVertexInputBindingDescription binding = { 0, sizeof(struct vertex), VK_VERTEX_INPUT_RATE_VERTEX };
   VkVertexInputAttributeDescription attributes[3] = {
      { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(struct vertex, pos) },
      { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(struct vertex, tex) },
      { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(struct vertex, color) }
   };

   VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
   vertex_input.vertexBindingDescriptionCount = 1;
   vertex_input.pVertexBindingDescriptions = &binding;
   vertex_input.vertexAttributeDescriptionCount = 3;
   vertex_input.pVertexAttributeDescriptions = attributes;

   VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
   input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

   VkViewport viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
   VkRect2D scissor = { {0, 0}, {width, height} };
   VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
   viewport_state.viewportCount = 1;
   viewport_state.pViewports = &viewport;
   viewport_state.scissorCount = 1;
   viewport_state.pScissors = &scissor;

   VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
   raster.polygonMode = VK_POLYGON_MODE_FILL;
   raster.cullMode = VK_CULL_MODE_NONE; // Disable culling to ensure text is visible
   raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
   raster.lineWidth = 1.0f;

   VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
   multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

   VkPipelineColorBlendAttachmentState blend_attachment = {0};
   blend_attachment.blendEnable = VK_TRUE;
   blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
   blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
   blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
   blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
   blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

   VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
   blend.attachmentCount = 1;
   blend.pAttachments = &blend_attachment;

   VkGraphicsPipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
   pipeline_info.stageCount = 2;
   pipeline_info.pStages = stages;
   pipeline_info.pVertexInputState = &vertex_input;
   pipeline_info.pInputAssemblyState = &input_assembly;
   pipeline_info.pViewportState = &viewport_state;
   pipeline_info.pRasterizationState = &raster;
   pipeline_info.pMultisampleState = &multisample;
   pipeline_info.pColorBlendState = &blend;
   pipeline_info.layout = vk.pipeline_layout;
   pipeline_info.renderPass = vk.render_pass;
   pipeline_info.subpass = 0;

   res = vkCreateGraphicsPipelines(device, vk.pipeline_cache, 1, &pipeline_info, NULL, &vk.pipeline);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create graphics pipeline: %d\n", res);
   }

   vkDestroyShaderModule(device, vert_module, NULL);
   vkDestroyShaderModule(device, frag_module, NULL);

   fprintf(stderr, "[libretro-test]: Pipeline initialized\n");
}


static void init_render_pass(VkFormat format)
{
   fprintf(stderr, "[libretro-test]: Initializing render pass with format: %d\n", format);
   VkAttachmentDescription attachment = {0};
   attachment.format = format; // Use provided format (e.g., VK_FORMAT_B5G6R5_UNORM_PACK16 for RGB565)
   attachment.samples = VK_SAMPLE_COUNT_1_BIT;
   attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
   VkSubpassDescription subpass = {0};
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &color_ref;

   VkRenderPassCreateInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
   rp_info.attachmentCount = 1;
   rp_info.pAttachments = &attachment;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = &subpass;
   VkResult res = vkCreateRenderPass(vulkan->device, &rp_info, NULL, &vk.render_pass);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create render pass: %d\n", res);
   }
}



static void init_swapchain(void)
{
   VkDevice device = vulkan->device;

   for (unsigned i = 0; i < vk.num_swapchain_images; i++)
   {
      VkImageCreateInfo image = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };

      image.imageType = VK_IMAGE_TYPE_2D;
      image.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
      image.format = VK_FORMAT_R8G8B8A8_UNORM;
      image.extent.width = width;
      image.extent.height = height;
      image.extent.depth = 1;
      image.samples = VK_SAMPLE_COUNT_1_BIT;
      image.tiling = VK_IMAGE_TILING_OPTIMAL;
      image.usage =
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
         VK_IMAGE_USAGE_SAMPLED_BIT |
         VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      image.mipLevels = 1;
      image.arrayLayers = 1;

      vkCreateImage(device, &image, NULL, &vk.images[i].create_info.image);

      VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
      VkMemoryRequirements mem_reqs;

      vkGetImageMemoryRequirements(device, vk.images[i].create_info.image, &mem_reqs);
      alloc.allocationSize = mem_reqs.size;
      alloc.memoryTypeIndex = find_memory_type_from_requirements(
            mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      vkAllocateMemory(device, &alloc, NULL, &vk.image_memory[i]);
      vkBindImageMemory(device, vk.images[i].create_info.image, vk.image_memory[i], 0);

      vk.images[i].create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      vk.images[i].create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vk.images[i].create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
      vk.images[i].create_info.subresourceRange.baseMipLevel = 0;
      vk.images[i].create_info.subresourceRange.baseArrayLayer = 0;
      vk.images[i].create_info.subresourceRange.levelCount = 1;
      vk.images[i].create_info.subresourceRange.layerCount = 1;
      vk.images[i].create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      vk.images[i].create_info.components.r = VK_COMPONENT_SWIZZLE_R;
      vk.images[i].create_info.components.g = VK_COMPONENT_SWIZZLE_G;
      vk.images[i].create_info.components.b = VK_COMPONENT_SWIZZLE_B;
      vk.images[i].create_info.components.a = VK_COMPONENT_SWIZZLE_A;

      vkCreateImageView(device, &vk.images[i].create_info,
            NULL, &vk.images[i].image_view);
      vk.images[i].image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
      fb_info.renderPass = vk.render_pass;
      fb_info.attachmentCount = 1;
      fb_info.pAttachments = &vk.images[i].image_view;
      fb_info.width = width;
      fb_info.height = height;
      fb_info.layers = 1;

      vkCreateFramebuffer(device, &fb_info, NULL, &vk.framebuffers[i]);
   }
}


// In vulkan_test_init, after init_swapchain


static void init_font(void)
{
   const char *system_dir = NULL;
   char font_path[512];

   if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) || !system_dir) {
      fprintf(stderr, "[libretro-test]: Failed to get system directory, falling back to current directory\n");
      system_dir = ".";
   }
   fprintf(stderr, "[libretro-test]: System directory: %s\n", system_dir);

   snprintf(font_path, sizeof(font_path), "%s/Kenney Pixel.ttf", system_dir);
   fprintf(stderr, "[libretro-test]: Attempting to load font from: %s\n", font_path);

   FILE* font_file = fopen(font_path, "rb");
   if (!font_file) {
      fprintf(stderr, "[libretro-test]: Failed to load Kenney Pixel.ttf from %s\n", font_path);
      snprintf(font_path, sizeof(font_path), "D:\\devcprojects\\libretro_vulkan_sample\\Kenney Pixel.ttf");
      fprintf(stderr, "[libretro-test]: Trying fallback path: %s\n", font_path);
      font_file = fopen(font_path, "rb");
      if (!font_file) {
         fprintf(stderr, "[libretro-test]: Failed to load Kenney Pixel.ttf from fallback path\n");
         snprintf(font_path, sizeof(font_path), "D:\\devcprojects\\libretro_vulkan_sample\\build\\Kenney Pixel.ttf");
         fprintf(stderr, "[libretro-test]: Trying build directory path: %s\n", font_path);
         font_file = fopen(font_path, "rb");
         if (!font_file) {
            fprintf(stderr, "[libretro-test]: Failed to load Kenney Pixel.ttf from build directory\n");
            return;
         }
      }
   }

   fseek(font_file, 0, SEEK_END);
   size_t font_size = ftell(font_file);
   fseek(font_file, 0, SEEK_SET);
   unsigned char* font_buffer = malloc(font_size);
   if (!font_buffer) {
      fprintf(stderr, "[libretro-test]: Failed to allocate font buffer\n");
      fclose(font_file);
      return;
   }
   if (fread(font_buffer, 1, font_size, font_file) != font_size) {
      fprintf(stderr, "[libretro-test]: Failed to read font file\n");
      free(font_buffer);
      fclose(font_file);
      return;
   }
   fclose(font_file);

   font.atlas = malloc(ATLAS_WIDTH * ATLAS_HEIGHT);
   if (!font.atlas) {
      fprintf(stderr, "[libretro-test]: Failed to allocate font atlas\n");
      free(font_buffer);
      return;
   }
   int bake_result = stbtt_BakeFontBitmap(font_buffer, 0, FONT_SIZE, font.atlas, ATLAS_WIDTH, ATLAS_HEIGHT, 32, 96, font.cdata);
   free(font_buffer);
   if (bake_result <= 0) {
      fprintf(stderr, "[libretro-test]: Failed to bake font bitmap: %d\n", bake_result);
      free(font.atlas);
      return;
   }

   VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
   image_info.imageType = VK_IMAGE_TYPE_2D;
   image_info.format = VK_FORMAT_R8_UNORM;
   image_info.extent.width = ATLAS_WIDTH;
   image_info.extent.height = ATLAS_HEIGHT;
   image_info.extent.depth = 1;
   image_info.mipLevels = 1;
   image_info.arrayLayers = 1;
   image_info.samples = VK_SAMPLE_COUNT_1_BIT;
   image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
   image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   VkResult res = vkCreateImage(vulkan->device, &image_info, NULL, &font.atlas_image);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create font atlas image: %d\n", res);
      free(font.atlas);
      return;
   }

   VkMemoryRequirements mem_reqs;
   vkGetImageMemoryRequirements(vulkan->device, font.atlas_image, &mem_reqs);
   VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
   alloc_info.allocationSize = mem_reqs.size;
   alloc_info.memoryTypeIndex = find_memory_type_from_requirements(
       mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   res = vkAllocateMemory(vulkan->device, &alloc_info, NULL, &font.atlas_memory);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to allocate font atlas memory: %d\n", res);
      vkDestroyImage(vulkan->device, font.atlas_image, NULL);
      free(font.atlas);
      return;
   }
   res = vkBindImageMemory(vulkan->device, font.atlas_image, font.atlas_memory, 0);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to bind font atlas memory: %d\n", res);
      vkFreeMemory(vulkan->device, font.atlas_memory, NULL);
      vkDestroyImage(vulkan->device, font.atlas_image, NULL);
      free(font.atlas);
      return;
   }

   VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
   view_info.image = font.atlas_image;
   view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
   view_info.format = VK_FORMAT_R8_UNORM;
   view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   view_info.subresourceRange.levelCount = 1;
   view_info.subresourceRange.layerCount = 1;
   res = vkCreateImageView(vulkan->device, &view_info, NULL, &font.atlas_image_view);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create font atlas image view: %d\n", res);
      vkFreeMemory(vulkan->device, font.atlas_memory, NULL);
      vkDestroyImage(vulkan->device, font.atlas_image, NULL);
      free(font.atlas);
      return;
   }

   VkSamplerCreateInfo sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
   sampler_info.magFilter = VK_FILTER_LINEAR;
   sampler_info.minFilter = VK_FILTER_LINEAR;
   sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
   sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   res = vkCreateSampler(vulkan->device, &sampler_info, NULL, &font.sampler);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create font sampler: %d\n", res);
      vkDestroyImageView(vulkan->device, font.atlas_image_view, NULL);
      vkFreeMemory(vulkan->device, font.atlas_memory, NULL);
      vkDestroyImage(vulkan->device, font.atlas_image, NULL);
      free(font.atlas);
      return;
   }

   struct buffer staging = create_buffer(font.atlas, ATLAS_WIDTH * ATLAS_HEIGHT, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
   if (!staging.buffer || !staging.memory) {
      fprintf(stderr, "[libretro-test]: Failed to create staging buffer for font atlas\n");
      vkDestroySampler(vulkan->device, font.sampler, NULL);
      vkDestroyImageView(vulkan->device, font.atlas_image_view, NULL);
      vkFreeMemory(vulkan->device, font.atlas_memory, NULL);
      vkDestroyImage(vulkan->device, font.atlas_image, NULL);
      free(font.atlas);
      return;
   }

   VkCommandBuffer cmd = vk.cmd[0];
   if (!cmd) {
      fprintf(stderr, "[libretro-test]: Command buffer vk.cmd[0] is null\n");
      vkFreeMemory(vulkan->device, staging.memory, NULL);
      vkDestroyBuffer(vulkan->device, staging.buffer, NULL);
      vkDestroySampler(vulkan->device, font.sampler, NULL);
      vkDestroyImageView(vulkan->device, font.atlas_image_view, NULL);
      vkFreeMemory(vulkan->device, font.atlas_memory, NULL);
      vkDestroyImage(vulkan->device, font.atlas_image, NULL);
      free(font.atlas);
      return;
   }

   VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
   res = vkBeginCommandBuffer(cmd, &begin_info);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to begin command buffer: %d\n", res);
      vkFreeMemory(vulkan->device, staging.memory, NULL);
      vkDestroyBuffer(vulkan->device, staging.buffer, NULL);
      vkDestroySampler(vulkan->device, font.sampler, NULL);
      vkDestroyImageView(vulkan->device, font.atlas_image_view, NULL);
      vkFreeMemory(vulkan->device, font.atlas_memory, NULL);
      vkDestroyImage(vulkan->device, font.atlas_image, NULL);
      free(font.atlas);
      return;
   }

   VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
   barrier.image = font.atlas_image;
   barrier.srcAccessMask = 0;
   barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   barrier.subresourceRange.levelCount = 1;
   barrier.subresourceRange.layerCount = 1;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

   VkBufferImageCopy copy = {0};
   copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   copy.imageSubresource.layerCount = 1;
   copy.imageExtent.width = ATLAS_WIDTH;
   copy.imageExtent.height = ATLAS_HEIGHT;
   copy.imageExtent.depth = 1;
   vkCmdCopyBufferToImage(cmd, staging.buffer, font.atlas_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

   barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

   res = vkEndCommandBuffer(cmd);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to end command buffer: %d\n", res);
      vkFreeMemory(vulkan->device, staging.memory, NULL);
      vkDestroyBuffer(vulkan->device, staging.buffer, NULL);
      vkDestroySampler(vulkan->device, font.sampler, NULL);
      vkDestroyImageView(vulkan->device, font.atlas_image_view, NULL);
      vkFreeMemory(vulkan->device, font.atlas_memory, NULL);
      vkDestroyImage(vulkan->device, font.atlas_image, NULL);
      free(font.atlas);
      return;
   }

   VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
   submit.commandBufferCount = 1;
   submit.pCommandBuffers = &cmd;
   res = vkQueueSubmit(vulkan->queue, 1, &submit, VK_NULL_HANDLE);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to submit queue: %d\n", res);
      vkFreeMemory(vulkan->device, staging.memory, NULL);
      vkDestroyBuffer(vulkan->device, staging.buffer, NULL);
      vkDestroySampler(vulkan->device, font.sampler, NULL);
      vkDestroyImageView(vulkan->device, font.atlas_image_view, NULL);
      vkFreeMemory(vulkan->device, font.atlas_memory, NULL);
      vkDestroyImage(vulkan->device, font.atlas_image, NULL);
      free(font.atlas);
      return;
   }

   res = vkQueueWaitIdle(vulkan->queue);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to wait for queue idle: %d\n", res);
   }

   vkFreeMemory(vulkan->device, staging.memory, NULL);
   vkDestroyBuffer(vulkan->device, staging.buffer, NULL);

   fprintf(stderr, "[libretro-test]: Font initialization completed\n");
}


static void init_command(void)
{
   VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
   VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };

   pool_info.queueFamilyIndex = vulkan->queue_index;
   pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

   for (unsigned i = 0; i < vk.num_swapchain_images; i++)
   {
      VkResult res = vkCreateCommandPool(vulkan->device, &pool_info, NULL, &vk.cmd_pool[i]);
      if (res != VK_SUCCESS) {
         fprintf(stderr, "[libretro-test]: Failed to create command pool %u: %d\n", i, res);
         return;
      }

      info.commandPool = vk.cmd_pool[i];
      info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      info.commandBufferCount = 1;
      res = vkAllocateCommandBuffers(vulkan->device, &info, &vk.cmd[i]);
      if (res != VK_SUCCESS) {
         fprintf(stderr, "[libretro-test]: Failed to allocate command buffer %u: %d\n", i, res);
         vkDestroyCommandPool(vulkan->device, vk.cmd_pool[i], NULL);
         return;
      }
   }
   fprintf(stderr, "[libretro-test]: Command pools and buffers initialized\n");
}


static void vulkan_test_init(void)
{
   vkGetPhysicalDeviceProperties(vulkan->gpu, &vk.gpu_properties);
   vkGetPhysicalDeviceMemoryProperties(vulkan->gpu, &vk.memory_properties);

   unsigned num_images = 0;
   uint32_t mask = vulkan->get_sync_index_mask(vulkan->handle);
   for (unsigned i = 0; i < 32; i++)
      if (mask & (1u << i))
         num_images = i + 1;
   vk.num_swapchain_images = num_images;
   vk.swapchain_mask = mask;

   fprintf(stderr, "[libretro-test]: Initializing Vulkan resources: %u swapchain images\n", num_images);

   init_command(); // Must be called before init_font
   init_font();
   init_uniform_buffer();
   init_text_vertex_buffer();
   init_descriptor();

   VkPipelineCacheCreateInfo pipeline_cache_info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
   VkResult res = vkCreatePipelineCache(vulkan->device, &pipeline_cache_info, NULL, &vk.pipeline_cache);
   if (res != VK_SUCCESS) {
      fprintf(stderr, "[libretro-test]: Failed to create pipeline cache: %d\n", res);
      return;
   }

   init_render_pass(VK_FORMAT_B5G6R5_UNORM_PACK16); // Match RGB565
   init_pipeline();
   init_swapchain();

   fprintf(stderr, "[libretro-test]: Vulkan initialization completed\n");
}



static void vulkan_test_deinit(void)
{
   if (!vulkan)
      return;

   VkDevice device = vulkan->device;
   vkDeviceWaitIdle(device);

   for (unsigned i = 0; i < vk.num_swapchain_images; i++)
   {
      vkDestroyFramebuffer(device, vk.framebuffers[i], NULL);
      vkDestroyImageView(device, vk.images[i].image_view, NULL);
      vkFreeMemory(device, vk.image_memory[i], NULL);
      vkDestroyImage(device, vk.images[i].create_info.image, NULL);
      vkFreeMemory(device, vk.ubo[i].memory, NULL);
      vkDestroyBuffer(device, vk.ubo[i].buffer, NULL);
   }

   vkFreeDescriptorSets(device, vk.desc_pool, vk.num_swapchain_images, vk.desc_set);
   vkDestroyDescriptorPool(device, vk.desc_pool, NULL);
   vkDestroyRenderPass(device, vk.render_pass, NULL);
   vkDestroyPipeline(device, vk.pipeline, NULL);
   vkDestroyDescriptorSetLayout(device, vk.set_layout, NULL);
   vkDestroyPipelineLayout(device, vk.pipeline_layout, NULL);
   vkFreeMemory(device, vk.vbo.memory, NULL);
   vkDestroyBuffer(device, vk.vbo.buffer, NULL);
   vkFreeMemory(device, vk.ibo.memory, NULL); // Add index buffer cleanup
   vkDestroyBuffer(device, vk.ibo.buffer, NULL); // Add index buffer cleanup
   vkDestroyPipelineCache(device, vk.pipeline_cache, NULL);
   vkDestroySampler(device, font.sampler, NULL);
   vkDestroyImageView(device, font.atlas_image_view, NULL);
   vkFreeMemory(device, font.atlas_memory, NULL);
   vkDestroyImage(device, font.atlas_image, NULL);
   free(font.atlas);

   for (unsigned i = 0; i < vk.num_swapchain_images; i++)
   {
      vkFreeCommandBuffers(device, vk.cmd_pool[i], 1, &vk.cmd[i]);
      vkDestroyCommandPool(device, vk.cmd_pool[i], NULL);
   }

   memset(&vk, 0, sizeof(vk));
}


static void update_variables(void)
{
   struct retro_variable var = {
      .key = "testvulkan_resolution",
   };

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      char *pch;
      char str[100];
      snprintf(str, sizeof(str), "%s", var.value);
      
      pch = strtok(str, "x");
      if (pch)
         width = strtoul(pch, NULL, 0);
      pch = strtok(NULL, "x");
      if (pch)
         height = strtoul(pch, NULL, 0);

      fprintf(stderr, "[libretro-test]: Got size: %u x %u.\n", width, height);
   }
}



void retro_run(void)
{
   fprintf(stderr, "[libretro-test]: retro_run started\n");
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   input_poll_cb();

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
   {
      fprintf(stderr, "[libretro-test]: Up key pressed\n");
   }

   if (vulkan->get_sync_index_mask(vulkan->handle) != vk.swapchain_mask)
   {
      fprintf(stderr, "[libretro-test]: Swapchain mask changed, reinitializing\n");
      vulkan_test_deinit();
      vulkan_test_init();
   }

   vulkan->wait_sync_index(vulkan->handle);
   vk.index = vulkan->get_sync_index(vulkan->handle);
   fprintf(stderr, "[libretro-test]: Rendering frame, sync index: %u\n", vk.index);
   vulkan_test_render();
   vulkan->set_image(vulkan->handle, &vk.images[vk.index], 0, NULL, VK_QUEUE_FAMILY_IGNORED);
   vulkan->set_command_buffers(vulkan->handle, 1, &vk.cmd[vk.index]);
   video_cb(RETRO_HW_FRAME_BUFFER_VALID, width, height, 0);
   fprintf(stderr, "[libretro-test]: retro_run completed\n");
}



static void context_reset(void)
{
   fprintf(stderr, "[libretro-test]: Context reset started\n");
   if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void**)&vulkan) || !vulkan)
   {
      fprintf(stderr, "[libretro-test]: Failed to get HW rendering interface\n");
      return;
   }

   if (vulkan->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
   {
      fprintf(stderr, "[libretro-test]: HW render interface mismatch, expected %u, got %u\n",
            RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION, vulkan->interface_version);
      vulkan = NULL;
      return;
   }

   vulkan_symbol_wrapper_init(vulkan->get_instance_proc_addr);
   fprintf(stderr, "[libretro-test]: Vulkan symbol wrapper initialized\n");

   if (!vulkan_symbol_wrapper_load_core_instance_symbols(vulkan->instance))
   {
      fprintf(stderr, "[libretro-test]: Failed to load core instance symbols\n");
      vulkan = NULL;
      return;
   }

   if (!vulkan_symbol_wrapper_load_core_device_symbols(vulkan->device))
   {
      fprintf(stderr, "[libretro-test]: Failed to load core device symbols\n");
      vulkan = NULL;
      return;
   }

   vulkan_test_init();
   fprintf(stderr, "[libretro-test]: Context reset completed\n");
}


static void context_destroy(void)
{
   fprintf(stderr, "Context destroy!\n");
   vulkan_test_deinit();
   vulkan = NULL;
   memset(&vk, 0, sizeof(vk));
}

static const VkApplicationInfo *get_application_info(void)
{
   static const VkApplicationInfo info = {
      VK_STRUCTURE_TYPE_APPLICATION_INFO,
      NULL,
      "libretro-test-vulkan",
      0,
      "libretro-test-vulkan",
      0,
      VK_MAKE_VERSION(1, 0, 18),
   };
   return &info;
}

static bool retro_init_hw_context(void)
{
   hw_render.context_type = RETRO_HW_CONTEXT_VULKAN;
   hw_render.version_major = VK_MAKE_VERSION(1, 0, 18);
   hw_render.version_minor = 0;
   hw_render.context_reset = context_reset;
   hw_render.context_destroy = context_destroy;
   hw_render.cache_context = true;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   static const struct retro_hw_render_context_negotiation_interface_vulkan iface = {
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION,

      get_application_info,
      NULL,
   };

   environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void*)&iface);

   return true;
}

bool retro_load_game(const struct retro_game_info *info)
{
   update_variables();

   if (!retro_init_hw_context())
   {
      fprintf(stderr, "HW Context could not be initialized, exiting...\n");
      return false;
   }

   fprintf(stderr, "Loaded game!\n");
   (void)info;
   return true;
}

void retro_unload_game(void)
{}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_reset(void)
{}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}
