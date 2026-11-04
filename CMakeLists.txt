cmake_minimum_required(VERSION 3.20)
include(FetchContent)

project(CGlabs LANGUAGES C CXX)

add_library(engine STATIC source/graphics_internal.cpp)

target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_compile_options(engine PUBLIC -Wall -Wextra -Wno-missing-field-initializers)
target_compile_features(engine PUBLIC cxx_std_20)
target_include_directories(engine PUBLIC ${CMAKE_SOURCE_DIR}/source)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.5.1
)

FetchContent_Declare(
	vk-bootstrap
	GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
	GIT_TAG v1.4.321
)

FetchContent_Declare(
	VulkanMemoryAllocator
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG v3.4.0
)

FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.92.9
)

FetchContent_Declare(
	glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG 1.0.1
)

find_package(Vulkan REQUIRED)

set(GLFW_LIBRARY_TYPE STATIC)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)

FetchContent_MakeAvailable(
	glfw
	vk-bootstrap
	VulkanMemoryAllocator
	imgui
	glm
)

target_link_libraries(engine PUBLIC
	glfw
	Vulkan::Vulkan
	vk-bootstrap::vk-bootstrap
	GPUOpen::VulkanMemoryAllocator
	glm::glm
)

target_include_directories(engine PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_sources(engine PRIVATE
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
)

find_program(GLSLC glslc)

function(add_lab NAME)
	cmake_parse_arguments(LAB "" "" "SHADERS" ${ARGN})

	add_executable(${NAME} source/main.cpp source/${NAME}/application.cpp)
	target_link_libraries(${NAME} PRIVATE engine)
	target_compile_definitions(${NAME} PRIVATE SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders/${NAME}/")

	if(GLSLC)
		set(SHADER_BINARIES)
		foreach(SHADER_FILE ${LAB_SHADERS})
			set(SHADER_SOURCE ${CMAKE_SOURCE_DIR}/shaders/${NAME}/${SHADER_FILE})
			set(SHADER_BINARY ${SHADER_SOURCE}.spv)

			add_custom_command(
				OUTPUT ${SHADER_BINARY}
				COMMAND ${GLSLC} ${SHADER_SOURCE} -o ${SHADER_BINARY}
				DEPENDS ${SHADER_SOURCE}
				COMMENT "Compiling ${NAME}/${SHADER_FILE} shader"
			)
			list(APPEND SHADER_BINARIES ${SHADER_BINARY})
		endforeach()

		add_custom_target(${NAME}_shaders DEPENDS ${SHADER_BINARIES})
		add_dependencies(${NAME} ${NAME}_shaders)
	endif()
endfunction()

add_lab(lab1 SHADERS sphere.vert sphere.frag)
add_lab(lab2 SHADERS lit.vert lit.frag marker.vert marker.frag)
