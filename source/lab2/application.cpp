#include "application.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <imgui.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cmath>
namespace {
	struct Vertex {
		glm::vec3 position;
		glm::vec3 normal;
	};

	struct SceneUniforms {
		glm::mat4 view_proj;
		glm::vec4 camera_position;
		glm::vec4 ambient;
		glm::vec4 dir_light_direction;
		glm::vec4 dir_light_color;
		uint32_t point_light_count;
		uint32_t spot_light_count;
		uint32_t padding[2];
	};

	struct ModelUniforms {
		glm::mat4 model;
		glm::mat4 normal_matrix;
		glm::vec4 albedo;
		glm::vec4 specular;
	};

	struct GpuPointLight {
		glm::vec4 position;
		glm::vec4 color;
	};

	struct GpuSpotLight {
		glm::vec4 position;
		glm::vec4 direction;
		glm::vec4 color;
		glm::vec4 cutoff;
	};

	struct MarkerPushConstants {
		glm::mat4 model;
		glm::vec4 color;
	};

	static_assert(sizeof(SceneUniforms) == 144);
	static_assert(sizeof(ModelUniforms) == 160);
	static_assert(sizeof(GpuPointLight) == 32);
	static_assert(sizeof(GpuSpotLight) == 64);

	struct Camera {
		glm::vec3 position = { 0.0f, 1.5f, 7.0f };
		float yaw = 0.0f;
		float pitch = -10.0f;
		float fov = 60.0f;
		float move_speed = 3.0f;
		float look_sensitivity = 0.15f;
	};

	struct Material {
		glm::vec3 albedo;
		glm::vec3 specular;
		float shininess;
	};

	struct DirectionalLight {
		glm::vec3 direction = { -0.4f, -1.0f, -0.3f };
		glm::vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 0.4f;
	};

	struct PointLight {
		glm::vec3 position = { 0.0f, 1.0f, 2.5f };
		glm::vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 5.0f;
	};

	struct SpotLight {
		glm::vec3 position = { 0.0f, 4.0f, 0.0f };
		glm::vec3 direction = { 0.0f, -1.0f, 0.0f };
		glm::vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 20.0f;
		float inner_angle = 15.0f;
		float outer_angle = 25.0f;
	};

	struct SceneState {
		Camera camera;

		glm::vec3 ambient_color = { 1.0f, 1.0f, 1.0f };
		float ambient_intensity = 0.05f;

		DirectionalLight dir_light;

		std::vector<PointLight> point_lights = {
			{ .position = { 2.5f, 1.0f, 2.0f }, .color = { 1.0f, 0.5f, 0.2f }, .intensity = 8.0f },
		};
		bool orbit_point_lights = false;
		float orbit_speed = 1.0f;

		std::vector<SpotLight> spot_lights = {
			{ .position = { -1.5f, 4.0f, 1.0f }, .direction = { 0.3f, -1.0f, -0.2f }, .color = { 0.3f, 0.6f, 1.0f }, .intensity = 30.0f },
		};

		glm::vec3 position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
		Material sphere_material = { .albedo = { 0.8f, 0.3f, 0.2f }, .specular = { 0.6f, 0.6f, 0.6f }, .shininess = 64.0f };

		bool anim_playing = true;
		float anim_speed = 1.0f;
		float anim_radius = 1.0f;
		float anim_height = 0.5f;
		float anim_spin = 90.0f;
		float anim_phase = 0.0f;

		Material floor_material = { .albedo = { 0.6f, 0.6f, 0.6f }, .specular = { 0.2f, 0.2f, 0.2f }, .shininess = 16.0f };
	};

	constexpr float floor_height = -2.0f;
	constexpr float floor_half_size = 10.0f;
	constexpr float marker_scale = 0.1f;
	constexpr glm::vec3 world_up = { 0.0f, 1.0f, 0.0f };

	struct GpuBuffer {
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkDeviceSize size = 0;
	};

	struct Mesh {
		GpuBuffer vertices;
		GpuBuffer indices;
		uint32_t index_count = 0;
	};

	struct RenderObject {
		const Mesh* mesh = nullptr;
		GpuBuffer uniforms;
		VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	};

	VkDescriptorSetLayout vk_scene_set_layout;
	VkDescriptorSetLayout vk_model_set_layout;
	VkDescriptorPool vk_descriptor_pool;
	VkDescriptorSet vk_scene_descriptor_set;

	VkPipelineLayout vk_lit_pipeline_layout;
	VkPipeline vk_lit_pipeline;
	VkPipelineLayout vk_marker_pipeline_layout;
	VkPipeline vk_marker_pipeline;

	GpuBuffer scene_uniform_buffer;
	GpuBuffer point_light_buffer;
	GpuBuffer spot_light_buffer;

	Mesh sphere_mesh;
	Mesh floor_mesh;
	RenderObject sphere_object;
	RenderObject floor_object;

	SceneState scene;
	double last_time = -1.0;

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
					radius * std::sin(phi) * std::cos(theta),
					radius * std::cos(phi),
					radius * std::sin(phi) * std::sin(theta),
				};

				const glm::vec3 normal = position / radius;

				vertices.push_back({ position, normal });
			}
		}

		for (uint32_t i = 0; i < stacks; ++i) {
			for (uint32_t j = 0; j < slices; ++j) {
				const uint32_t a = i * (slices + 1) + j;
				const uint32_t b = i * (slices + 1) + (j + 1);
				const uint32_t c = (i + 1) * (slices + 1) + j;
				const uint32_t d = (i + 1) * (slices + 1) + (j + 1);

				indices.push_back(a);
				indices.push_back(b);
				indices.push_back(c);

				indices.push_back(c);
				indices.push_back(b);
				indices.push_back(d);
			}
		}
	}

	void generatePlane(float half_size, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
		const float s = half_size;
		vertices = {
			{ { -s, 0.0f, -s }, world_up },
			{ { -s, 0.0f,  s }, world_up },
			{ {  s, 0.0f,  s }, world_up },
			{ {  s, 0.0f, -s }, world_up },
		};
		indices = { 0, 1, 2, 0, 2, 3 };
	}

	bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, GpuBuffer& out) {
		const VkBufferCreateInfo buffer_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
		};

		const VmaAllocationCreateInfo alloc_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		const auto result = vmaCreateBuffer(graphics::internal::context.allocator, &buffer_info, &alloc_info, &out.buffer, &out.allocation, nullptr);
		if (result != VK_SUCCESS) {
			std::cerr << "Failed to create buffer\n";
			return false;
		}

		out.size = size;
		return true;
	}

	bool uploadBuffer(const GpuBuffer& buffer, const void* data, VkDeviceSize size) {
		const auto result = vmaCopyMemoryToAllocation(graphics::internal::context.allocator, data, buffer.allocation, 0, size);
		if (result != VK_SUCCESS) {
			std::cerr << "Failed to copy data to buffer\n";
			return false;
		}
		return true;
	}

	void destroyBuffer(GpuBuffer& buffer) {
		if (buffer.buffer != VK_NULL_HANDLE) {
			vmaDestroyBuffer(graphics::internal::context.allocator, buffer.buffer, buffer.allocation);
		}
		buffer = {};
	}

	bool createMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, Mesh& mesh) {
		const VkDeviceSize vertices_size = vertices.size() * sizeof(Vertex);
		const VkDeviceSize indices_size = indices.size() * sizeof(uint32_t);

		if (!createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertices) ||
		    !uploadBuffer(mesh.vertices, vertices.data(), vertices_size)) {
			return false;
		}
		if (!createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.indices) ||
		    !uploadBuffer(mesh.indices, indices.data(), indices_size)) {
			return false;
		}

		mesh.index_count = static_cast<uint32_t>(indices.size());
		return true;
	}

	void destroyMesh(Mesh& mesh) {
		destroyBuffer(mesh.vertices);
		destroyBuffer(mesh.indices);
	}

	void writeBufferDescriptor(VkDescriptorSet set, uint32_t binding, VkDescriptorType type, const GpuBuffer& buffer) {
		const VkDescriptorBufferInfo buffer_info = {
			.buffer = buffer.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		const VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = set,
			.dstBinding = binding,
			.descriptorCount = 1,
			.descriptorType = type,
			.pBufferInfo = &buffer_info,
		};

		vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
	}

	bool ensureStorageCapacity(GpuBuffer& buffer, size_t count, VkDeviceSize element_size, uint32_t binding) {
		const VkDeviceSize required = std::max<VkDeviceSize>(count, 1) * element_size;
		if (buffer.buffer != VK_NULL_HANDLE && buffer.size >= required) {
			return true;
		}

		const VkDeviceSize new_size = std::max(required, buffer.size * 2);

		vkQueueWaitIdle(graphics::internal::context.graphics_queue);
		destroyBuffer(buffer);

		if (!createBuffer(new_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, buffer)) {
			return false;
		}

		writeBufferDescriptor(vk_scene_descriptor_set, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer);
		return true;
	}

	bool createShaderModule(const char* path, VkShaderModule& module) {
		const auto code = loadSpirv(path);
		if (code.empty()) {
			return false;
		}

		const VkShaderModuleCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = code.size() * sizeof(uint32_t),
			.pCode = code.data(),
		};

		if (vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &module) != VK_SUCCESS) {
			std::cerr << "Failed to create shader module: " << path << '\n';
			return false;
		}
		return true;
	}

	bool createGraphicsPipeline(const char* vert_path, const char* frag_path, VkPipelineLayout layout, VkPipeline& pipeline, uint32_t attribute_count = 2) {
		auto& context = graphics::internal::context;

		VkShaderModule vert_module;
		VkShaderModule frag_module;
		if (!createShaderModule(vert_path, vert_module)) {
			return false;
		}
		if (!createShaderModule(frag_path, frag_module)) {
			vkDestroyShaderModule(context.device, vert_module, nullptr);
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
			{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
			{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
		};

		const VkPipelineVertexInputStateCreateInfo vertex_input = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &binding,
			.vertexAttributeDescriptionCount = attribute_count,
			.pVertexAttributeDescriptions = attributes,
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
			.layout = layout,
			.renderPass = context.render_pass,
		};
		const auto result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);

		vkDestroyShaderModule(context.device, vert_module, nullptr);
		vkDestroyShaderModule(context.device, frag_module, nullptr);

		if (result != VK_SUCCESS) {
			std::cerr << "Failed to create graphics pipeline\n";
			return false;
		}
		return true;
	}

	glm::vec3 cameraForward(const Camera& camera) {
		const float yaw = glm::radians(camera.yaw);
		const float pitch = glm::radians(camera.pitch);
		return {
			std::cos(pitch) * std::sin(yaw),
			std::sin(pitch),
			-std::cos(pitch) * std::cos(yaw),
		};
	}

	void updateCamera(float dt) {
		Camera& camera = scene.camera;
		const ImGuiIO& io = ImGui::GetIO();

		if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			camera.yaw += io.MouseDelta.x * camera.look_sensitivity;
			camera.pitch -= io.MouseDelta.y * camera.look_sensitivity;
		}
		camera.yaw = std::remainder(camera.yaw, 360.0f);
		camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);

		if (io.WantCaptureKeyboard) {
			return;
		}

		const glm::vec3 forward = cameraForward(camera);
		const glm::vec3 right = glm::normalize(glm::cross(forward, world_up));

		glm::vec3 move(0.0f);
		if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
		if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
		if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
		if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
		if (ImGui::IsKeyDown(ImGuiKey_E)) move += world_up;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= world_up;

		if (glm::length(move) > 0.0f) {
			camera.position += glm::normalize(move) * camera.move_speed * dt;
		}
	}

	glm::mat4 sphereModelMatrix() {
		const glm::vec3 trajectory = {
			scene.anim_radius * std::cos(scene.anim_phase),
			scene.anim_height * std::sin(2.0f * scene.anim_phase),
			scene.anim_radius * std::sin(scene.anim_phase),
		};
		const float spin = scene.anim_spin * scene.anim_phase;

		glm::mat4 model = glm::translate(glm::mat4(1.0f), scene.position + trajectory);
		model = glm::rotate(model, glm::radians(scene.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		model = glm::rotate(model, glm::radians(scene.rotation.y + spin), glm::vec3(0.0f, 1.0f, 0.0f));
		model = glm::rotate(model, glm::radians(scene.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		model = glm::scale(model, scene.scale);
		return model;
	}

	void updateObject(RenderObject& object, const glm::mat4& model, const Material& material) {
		const ModelUniforms uniforms = {
			.model = model,
			.normal_matrix = glm::transpose(glm::inverse(model)),
			.albedo = glm::vec4(material.albedo, 1.0f),
			.specular = glm::vec4(material.specular, material.shininess),
		};
		uploadBuffer(object.uniforms, &uniforms, sizeof(uniforms));
	}

	void drawMesh(VkCommandBuffer command_buffer, const Mesh& mesh) {
		const VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(command_buffer, 0, 1, &mesh.vertices.buffer, offsets);
		vkCmdBindIndexBuffer(command_buffer, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(command_buffer, mesh.index_count, 1, 0, 0, 0);
	}

	void materialUi(Material& material) {
		ImGui::ColorEdit3("Albedo", &material.albedo.x);
		ImGui::ColorEdit3("Specular", &material.specular.x);
		ImGui::SliderFloat("Shininess", &material.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
	}

	void cameraUi() {
		Camera& camera = scene.camera;
		ImGui::TextDisabled("Hold RMB to look around, WASD/QE to move");
		ImGui::DragFloat3("Position", &camera.position.x, 0.05f);
		ImGui::SliderFloat("Yaw", &camera.yaw, -180.0f, 180.0f);
		ImGui::SliderFloat("Pitch", &camera.pitch, -89.0f, 89.0f);
		ImGui::SliderFloat("FOV", &camera.fov, 20.0f, 120.0f);
		ImGui::SliderFloat("Move speed", &camera.move_speed, 0.5f, 20.0f);
	}

	void lightingUi() {
		ImGui::SeparatorText("Ambient");
		ImGui::ColorEdit3("Ambient color", &scene.ambient_color.x);
		ImGui::SliderFloat("Ambient intensity", &scene.ambient_intensity, 0.0f, 1.0f);

		ImGui::SeparatorText("Directional");
		ImGui::DragFloat3("Direction", &scene.dir_light.direction.x, 0.01f, -1.0f, 1.0f);
		ImGui::ColorEdit3("Color", &scene.dir_light.color.x);
		ImGui::SliderFloat("Intensity", &scene.dir_light.intensity, 0.0f, 5.0f);
	}

	void pointLightsUi() {
		ImGui::Checkbox("Orbit around Y", &scene.orbit_point_lights);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::SliderFloat("Orbit speed", &scene.orbit_speed, -3.0f, 3.0f);

		int remove_index = -1;
		for (size_t i = 0; i < scene.point_lights.size(); ++i) {
			PointLight& light = scene.point_lights[i];
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::TreeNode("light", "Point light %zu", i)) {
				ImGui::DragFloat3("Position", &light.position.x, 0.05f);
				ImGui::ColorEdit3("Color", &light.color.x);
				ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 100.0f);
				if (ImGui::Button("Remove")) {
					remove_index = static_cast<int>(i);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (remove_index >= 0) {
			scene.point_lights.erase(scene.point_lights.begin() + remove_index);
		}
		if (ImGui::Button("Add point light")) {
			scene.point_lights.push_back({});
		}
	}

	void spotLightsUi() {
		int remove_index = -1;
		for (size_t i = 0; i < scene.spot_lights.size(); ++i) {
			SpotLight& light = scene.spot_lights[i];
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::TreeNode("light", "Spot light %zu", i)) {
				ImGui::DragFloat3("Position", &light.position.x, 0.05f);
				ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f);
				ImGui::ColorEdit3("Color", &light.color.x);
				ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 200.0f);
				ImGui::SliderFloat("Inner angle", &light.inner_angle, 0.0f, 89.0f);
				ImGui::SliderFloat("Outer angle", &light.outer_angle, 0.0f, 89.0f);
				light.outer_angle = std::max(light.outer_angle, light.inner_angle);
				if (ImGui::Button("Remove")) {
					remove_index = static_cast<int>(i);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (remove_index >= 0) {
			scene.spot_lights.erase(scene.spot_lights.begin() + remove_index);
		}
		if (ImGui::Button("Add spot light")) {
			scene.spot_lights.push_back({});
		}
	}

	void sphereUi() {
		ImGui::SeparatorText("Transform");
		ImGui::DragFloat3("Position", &scene.position.x, 0.01f);
		ImGui::DragFloat3("Rotation", &scene.rotation.x, 1.0f);
		ImGui::DragFloat3("Scale", &scene.scale.x, 0.01f, 0.01f, 10.0f);

		ImGui::SeparatorText("Material");
		materialUi(scene.sphere_material);

		ImGui::SeparatorText("Animation");
		if (ImGui::Button(scene.anim_playing ? "Pause" : "Play")) {
			scene.anim_playing = !scene.anim_playing;
		}
		ImGui::SliderFloat("Speed", &scene.anim_speed, -5.0f, 5.0f);
		ImGui::SliderFloat("Radius", &scene.anim_radius, 0.0f, 3.0f);
		ImGui::SliderFloat("Height", &scene.anim_height, 0.0f, 2.0f);
		ImGui::SliderFloat("Spin", &scene.anim_spin, -360.0f, 360.0f);
	}
}

namespace application {

bool initialize() {
	auto& context = graphics::internal::context;

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	generateSphere(1.0f, 32, 64, vertices, indices);
	if (!createMesh(vertices, indices, sphere_mesh)) {
		return false;
	}

	generatePlane(floor_half_size, vertices, indices);
	if (!createMesh(vertices, indices, floor_mesh)) {
		return false;
	}

	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
		{ .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
		{ .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
	};
	const VkDescriptorSetLayoutCreateInfo scene_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(scene_bindings) / sizeof(scene_bindings[0]),
		.pBindings = scene_bindings,
	};
	if (vkCreateDescriptorSetLayout(context.device, &scene_layout_info, nullptr, &vk_scene_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create scene descriptor set layout\n";
		return false;
	}

	const VkDescriptorSetLayoutBinding model_binding = {
		.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	const VkDescriptorSetLayoutCreateInfo model_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &model_binding,
	};
	if (vkCreateDescriptorSetLayout(context.device, &model_layout_info, nullptr, &vk_model_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create model descriptor set layout\n";
		return false;
	}

	RenderObject* const objects[] = { &sphere_object, &floor_object };
	constexpr uint32_t object_count = sizeof(objects) / sizeof(objects[0]);

	const VkDescriptorPoolSize pool_sizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 + object_count },
		{ .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
	};
	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1 + object_count,
		.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
		.pPoolSizes = pool_sizes,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	const VkDescriptorSetLayout set_layouts[] = { vk_scene_set_layout, vk_model_set_layout, vk_model_set_layout };
	VkDescriptorSet sets[1 + object_count];
	const VkDescriptorSetAllocateInfo set_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_descriptor_pool,
		.descriptorSetCount = 1 + object_count,
		.pSetLayouts = set_layouts,
	};
	if (vkAllocateDescriptorSets(context.device, &set_alloc_info, sets) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor sets\n";
		return false;
	}
	vk_scene_descriptor_set = sets[0];

	if (!createBuffer(sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, scene_uniform_buffer)) {
		return false;
	}
	writeBufferDescriptor(vk_scene_descriptor_set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, scene_uniform_buffer);

	if (!ensureStorageCapacity(point_light_buffer, 4, sizeof(GpuPointLight), 1) ||
	    !ensureStorageCapacity(spot_light_buffer, 4, sizeof(GpuSpotLight), 2)) {
		return false;
	}

	sphere_object.mesh = &sphere_mesh;
	floor_object.mesh = &floor_mesh;
	for (uint32_t i = 0; i < object_count; ++i) {
		objects[i]->descriptor_set = sets[1 + i];
		if (!createBuffer(sizeof(ModelUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, objects[i]->uniforms)) {
			return false;
		}
		writeBufferDescriptor(objects[i]->descriptor_set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, objects[i]->uniforms);
	}

	const VkDescriptorSetLayout lit_set_layouts[] = { vk_scene_set_layout, vk_model_set_layout };
	const VkPipelineLayoutCreateInfo lit_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeof(lit_set_layouts) / sizeof(lit_set_layouts[0]),
		.pSetLayouts = lit_set_layouts,
	};
	if (vkCreatePipelineLayout(context.device, &lit_layout_info, nullptr, &vk_lit_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create lit pipeline layout\n";
		return false;
	}

	const VkPushConstantRange marker_push_range = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(MarkerPushConstants),
	};
	const VkPipelineLayoutCreateInfo marker_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vk_scene_set_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &marker_push_range,
	};
	if (vkCreatePipelineLayout(context.device, &marker_layout_info, nullptr, &vk_marker_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create marker pipeline layout\n";
		return false;
	}

	if (!createGraphicsPipeline(SHADER_DIR "lit.vert.spv", SHADER_DIR "lit.frag.spv", vk_lit_pipeline_layout, vk_lit_pipeline) ||
	    !createGraphicsPipeline(SHADER_DIR "marker.vert.spv", SHADER_DIR "marker.frag.spv", vk_marker_pipeline_layout, vk_marker_pipeline, 1)) {
		return false;
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, vk_marker_pipeline, nullptr);
	vkDestroyPipeline(context.device, vk_lit_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, vk_marker_pipeline_layout, nullptr);
	vkDestroyPipelineLayout(context.device, vk_lit_pipeline_layout, nullptr);

	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_scene_set_layout, nullptr);

	destroyBuffer(sphere_object.uniforms);
	destroyBuffer(floor_object.uniforms);
	destroyBuffer(spot_light_buffer);
	destroyBuffer(point_light_buffer);
	destroyBuffer(scene_uniform_buffer);

	destroyMesh(floor_mesh);
	destroyMesh(sphere_mesh);
}

void update(double time) {
	const float dt = last_time < 0.0 ? 0.0f : static_cast<float>(time - last_time);
	last_time = time;

	updateCamera(dt);

	if (scene.anim_playing) {
		scene.anim_phase += dt * scene.anim_speed;
	}

	if (scene.orbit_point_lights) {
		const glm::mat4 orbit = glm::rotate(glm::mat4(1.0f), dt * scene.orbit_speed, world_up);
		for (PointLight& light : scene.point_lights) {
			light.position = glm::vec3(orbit * glm::vec4(light.position, 1.0f));
		}
	}

	ImGui::Begin("Scene");

	ImGui::PushID("camera");
	if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		cameraUi();
	}
	ImGui::PopID();

	ImGui::PushID("lighting");
	if (ImGui::CollapsingHeader("Ambient & directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
		lightingUi();
	}
	ImGui::PopID();

	ImGui::PushID("point");
	if (ImGui::CollapsingHeader("Point lights")) {
		pointLightsUi();
	}
	ImGui::PopID();

	ImGui::PushID("spot");
	if (ImGui::CollapsingHeader("Spot lights")) {
		spotLightsUi();
	}
	ImGui::PopID();

	ImGui::PushID("sphere");
	if (ImGui::CollapsingHeader("Sphere")) {
		sphereUi();
	}
	ImGui::PopID();

	ImGui::PushID("floor");
	if (ImGui::CollapsingHeader("Floor")) {
		materialUi(scene.floor_material);
	}
	ImGui::PopID();

	ImGui::Separator();
	if (ImGui::Button("Reset scene")) {
		scene = SceneState{};
	}

	ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	const float aspect = static_cast<float>(context.swapchain_extent.width) / static_cast<float>(context.swapchain_extent.height);

	const Camera& camera = scene.camera;
	const glm::mat4 view = glm::lookAt(camera.position, camera.position + cameraForward(camera), world_up);
	glm::mat4 proj = glm::perspective(glm::radians(camera.fov), aspect, 0.1f, 100.0f);
	proj[1][1] *= -1.0f;

	const SceneUniforms scene_uniforms = {
		.view_proj = proj * view,
		.camera_position = glm::vec4(camera.position, 1.0f),
		.ambient = glm::vec4(scene.ambient_color * scene.ambient_intensity, 0.0f),
		.dir_light_direction = glm::vec4(scene.dir_light.direction, 0.0f),
		.dir_light_color = glm::vec4(scene.dir_light.color * scene.dir_light.intensity, 0.0f),
		.point_light_count = static_cast<uint32_t>(scene.point_lights.size()),
		.spot_light_count = static_cast<uint32_t>(scene.spot_lights.size()),
	};
	uploadBuffer(scene_uniform_buffer, &scene_uniforms, sizeof(scene_uniforms));

	std::vector<GpuPointLight> point_lights;
	point_lights.reserve(scene.point_lights.size());
	for (const PointLight& light : scene.point_lights) {
		point_lights.push_back({
			.position = glm::vec4(light.position, 1.0f),
			.color = glm::vec4(light.color * light.intensity, 0.0f),
		});
	}
	ensureStorageCapacity(point_light_buffer, point_lights.size(), sizeof(GpuPointLight), 1);
	if (!point_lights.empty()) {
		uploadBuffer(point_light_buffer, point_lights.data(), point_lights.size() * sizeof(GpuPointLight));
	}

	std::vector<GpuSpotLight> spot_lights;
	spot_lights.reserve(scene.spot_lights.size());
	for (const SpotLight& light : scene.spot_lights) {
		const glm::vec3 direction = glm::length(light.direction) > 0.0f ? glm::normalize(light.direction) : -world_up;
		spot_lights.push_back({
			.position = glm::vec4(light.position, 1.0f),
			.direction = glm::vec4(direction, 0.0f),
			.color = glm::vec4(light.color * light.intensity, 0.0f),
			.cutoff = glm::vec4(std::cos(glm::radians(light.inner_angle)), std::cos(glm::radians(light.outer_angle)), 0.0f, 0.0f),
		});
	}
	ensureStorageCapacity(spot_light_buffer, spot_lights.size(), sizeof(GpuSpotLight), 2);
	if (!spot_lights.empty()) {
		uploadBuffer(spot_light_buffer, spot_lights.data(), spot_lights.size() * sizeof(GpuSpotLight));
	}

	updateObject(sphere_object, sphereModelMatrix(), scene.sphere_material);
	updateObject(floor_object, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, floor_height, 0.0f)), scene.floor_material);

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

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_lit_pipeline);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_lit_pipeline_layout, 0, 1, &vk_scene_descriptor_set, 0, nullptr);

	for (const RenderObject* object : { &sphere_object, &floor_object }) {
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_lit_pipeline_layout, 1, 1, &object->descriptor_set, 0, nullptr);
		drawMesh(fd.command_buffer, *object->mesh);
	}

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_marker_pipeline);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_marker_pipeline_layout, 0, 1, &vk_scene_descriptor_set, 0, nullptr);

	const auto drawMarker = [&](const glm::vec3& position, const glm::vec3& color) {
		const MarkerPushConstants pc = {
			.model = glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(marker_scale)),
			.color = glm::vec4(color, 1.0f),
		};
		vkCmdPushConstants(fd.command_buffer, vk_marker_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
		drawMesh(fd.command_buffer, sphere_mesh);
	};
	for (const PointLight& light : scene.point_lights) {
		drawMarker(light.position, light.color);
	}
	for (const SpotLight& light : scene.spot_lights) {
		drawMarker(light.position, light.color);
	}

	vkCmdEndRenderPass(fd.command_buffer);
}

}
