#include "application.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <imgui.h>
#include <fstream>
#include <iostream>
#include <cmath>
namespace {
	VkPipelineLayout vk_pipeline_layout;
	VkPipeline vk_pipeline;


	VkBuffer vk_index_buffer;
	VkBuffer vk_vertex_buffer;
	VmaAllocation vk_index_buffer_allocation;
	VmaAllocation vk_vertex_buffer_allocation;

	uint32_t vk_index_count;
	struct Vertex {
		glm::vec3 position;
		glm::vec3 color;
	};

	struct PushConstants {
		glm::mat4 mvp;
	};

	std::vector<uint32_t> loadSpirv(const char* path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) {
			std::cerr << "Failed to open SPIR-V file: " << path << '\n';
			return {};
		}

		const size_t size_bytes = file.tellg();
		std::vector<uint32_t> code(size_bytes / sizeof(uint32_t));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(code.data()), size_bytes);
		file.close();
		return code;
	}

	void generateSphere(float radius, uint32_t stacks, uint32_t slices, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
		for (uint32_t i = 0; i <= stacks; ++i) {
        	const float phi = static_cast<float>(M_PI) * i / stacks;

        	for (uint32_t j = 0; j <= slices; ++j) {
                      const float theta = static_cast<float>(2.0 * M_PI) * j / slices;

                      const glm::vec3 position = {
                              radius * std::sin(phi) * std::cos(theta), // x = r · sin(φ) · cos(θ)
                              radius * std::cos(phi),                   // y = r · cos(φ)
                              radius * std::sin(phi) * std::sin(theta), // z = r · sin(φ) · sin(θ)
                      };

                      const glm::vec3 color = {
							  (position.x + radius) / (2.0f * radius),
							  (position.y + radius) / (2.0f * radius),
							  (position.z + radius) / (2.0f * radius),
					  };

                      vertices.push_back({ position, color });
              }
      }

      for (uint32_t i = 0; i < stacks; ++i) {
              for (uint32_t j = 0; j < slices; ++j) {
                      const uint32_t a = i * (slices + 1) + j;
                      const uint32_t b = i * (slices + 1) + (j + 1);
                      const uint32_t c = (i+1) * (slices + 1) + j;
                      const uint32_t d = (i+1) * (slices + 1) + (j + 1);

                      indices.push_back(a);
					  indices.push_back(b);
					  indices.push_back(c);

					  indices.push_back(c);
					  indices.push_back(b);
					  indices.push_back(d);
              }
    	}
	}

	bool createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& allocation) {
    	const VkBufferCreateInfo buffer_info = {
        	.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        	.size = size,
        	.usage = usage,
      	};

    	const VmaAllocationCreateInfo alloc_info = {
    		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
    		.usage = VMA_MEMORY_USAGE_AUTO,
      	};



    	auto result = vmaCreateBuffer(graphics::internal::context.allocator, &buffer_info, &alloc_info, &buffer, &allocation, nullptr);
		if (result != VK_SUCCESS) {
			std::cerr << "Failed to create buffer\n";
			return false;
		}

		result = vmaCopyMemoryToAllocation(graphics::internal::context.allocator, data, allocation, 0, size);
		if (result != VK_SUCCESS) {
			std::cerr << "Failed to copy data to buffer\n";
			return false;
		}

    	return true;
	}
}
// namespace

namespace application {

bool initialize() {
	std::vector<Vertex> sphere_vertices;
    std::vector<uint32_t> sphere_indices;
    generateSphere(1.0f, 32, 40, sphere_vertices, sphere_indices);

	createBuffer(sphere_vertices.data(), sphere_vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vk_vertex_buffer, vk_vertex_buffer_allocation);
	createBuffer(sphere_indices.data(), sphere_indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, vk_index_buffer, vk_index_buffer_allocation);
	vk_index_count = static_cast<uint32_t>(sphere_indices.size());

	auto vert_code = loadSpirv(SHADER_DIR "sphere.vert.spv");
	auto frag_code = loadSpirv(SHADER_DIR "sphere.frag.spv");

	const VkShaderModuleCreateInfo vert_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = vert_code.size() * sizeof(uint32_t),
		.pCode = vert_code.data(),
	};

	VkShaderModule vert_module;

	const VkShaderModuleCreateInfo frag_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = frag_code.size() * sizeof(uint32_t),
		.pCode = frag_code.data(),
	};
	VkShaderModule frag_module;

	auto result = vkCreateShaderModule(graphics::internal::context.device, &vert_info, nullptr, &vert_module);
	if (result != VK_SUCCESS) {
		std::cerr << "Failed to create vertex shader module\n";
		return false;
	}

	result = vkCreateShaderModule(graphics::internal::context.device, &frag_info, nullptr, &frag_module);
	if (result != VK_SUCCESS) {
		std::cerr << "Failed to create fragment shader module\n";
		return false;
	}

	const VkPushConstantRange push_range = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(PushConstants),
	};

	const VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_range,
	};

	result = vkCreatePipelineLayout(graphics::internal::context.device, &pipeline_layout_info, nullptr, &vk_pipeline_layout);
	if (result != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		return false;
	}

	const VkPipelineShaderStageCreateInfo stages[] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" },
	};

	const VkVertexInputBindingDescription binding = {
    	.binding = 0,
    	.stride = sizeof(Vertex),
    	.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	const VkVertexInputAttributeDescription attributes[] = {
    	{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },   // position
    	{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) },   // color
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = sizeof(attributes) / sizeof(attributes[0]),
		.pVertexAttributeDescriptions = attributes
	};
	const VkPipelineInputAssemblyStateCreateInfo input_assembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
	const VkPipelineViewportStateCreateInfo viewport_state = {
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo rasterization = {
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    	.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_BACK_BIT, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    	.lineWidth = 1.0f,
	};


	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS,
	};

	const VkPipelineColorBlendAttachmentState blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	const VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend_attachment,
	};

	const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamic_states,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blend,
		.pDynamicState = &dynamic_state,
		.layout = vk_pipeline_layout,
		.renderPass = graphics::internal::context.render_pass,
	};
	result = vkCreateGraphicsPipelines(graphics::internal::context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &vk_pipeline);
	if (result != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline\n";
		return false;
	}

	vkDestroyShaderModule(graphics::internal::context.device, vert_module, nullptr);
	vkDestroyShaderModule(graphics::internal::context.device, frag_module, nullptr);

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);
	vmaDestroyBuffer(context.allocator, vk_vertex_buffer, vk_vertex_buffer_allocation);
	vmaDestroyBuffer(context.allocator, vk_index_buffer, vk_index_buffer_allocation);
	vkDestroyPipeline(context.device, vk_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, vk_pipeline_layout, nullptr);
}

void update([[maybe_unused]] double time) {
	ImGui::ShowDemoWindow();
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	const float aspect = static_cast<float>(context.swapchain_extent.width) / static_cast<float>(context.swapchain_extent.height);
	glm::mat4 model = glm::rotate(glm::mat4(1.0f), static_cast<float>(glfwGetTime()), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 view  = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                  glm::vec3(0.0f, 0.0f, 0.0f),
                                  glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 proj  = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
	proj[1][1] *= -1.0f;
	PushConstants pc = {
		.mvp = proj * view * model
	};

	const VkClearValue clear_values[] = {
		{ .color = { { 0.0f, 0.0f, 0.0f, 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = {
			.offset = { 0, 0 },
			.extent = context.swapchain_extent,
		},
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

	vkCmdPushConstants(fd.command_buffer, vk_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = static_cast<float>(context.swapchain_extent.width),
		.height = static_cast<float>(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

	const VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = context.swapchain_extent,
	};
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	const VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vk_vertex_buffer, offsets);
	vkCmdBindIndexBuffer(fd.command_buffer, vk_index_buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(fd.command_buffer, vk_index_count, 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);
}

} // namespace application