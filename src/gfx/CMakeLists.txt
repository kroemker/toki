find_program(TOKI_SHADERCROSS shadercross
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sdl3-shadercross"
    NO_DEFAULT_PATH REQUIRED)

# Only the formats this platform's backends read: D3D12 takes DXIL, Metal MSL,
# Vulkan SPIR-V. The MSL text alone is most of the embedded size.
if(APPLE)
    set(shaderFormats MSL SPIRV)
elseif(WIN32)
    set(shaderFormats DXIL SPIRV)
else()
    set(shaderFormats SPIRV)
endif()

set(ubershader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Ubershader.hlsl")
set(shaderDirectory "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(compiledShaders "")
set(embeddedShaders "")
foreach(stage Vertex Fragment)
    string(TOUPPER "${stage}" stageDefine)
    string(TOLOWER "${stage}" stageName)
    foreach(format SPIRV DXIL MSL)
        string(TOLOWER "${format}" formatName)
        string(SUBSTRING "${formatName}" 0 1 first)
        string(TOUPPER "${first}" first)
        string(SUBSTRING "${formatName}" 1 -1 rest)
        if(NOT format IN_LIST shaderFormats)
            list(APPEND embeddedShaders "${stage}${first}${rest}=")
            continue()
        endif()
        set(output "${shaderDirectory}/${stage}.${format}")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${shaderDirectory}"
            COMMAND "${TOKI_SHADERCROSS}" "${ubershader}" -s HLSL -d ${format} -t ${stageName}
                    -e ${stage}Main -D${stageDefine}_STAGE -o "${output}"
            DEPENDS "${ubershader}"
            COMMENT "Compiling the ${stageName} ubershader to ${format}"
            VERBATIM)
        list(APPEND compiledShaders "${output}")
        list(APPEND embeddedShaders "${stage}${first}${rest}=${output}")
    endforeach()
endforeach()

list(JOIN embeddedShaders "|" embeddedList)
set(shaderHeader "${CMAKE_CURRENT_BINARY_DIR}/generated/gfx/UbershaderCode.h")
add_custom_command(
    OUTPUT "${shaderHeader}"
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${shaderHeader} -DINPUTS=${embeddedList}
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS ${compiledShaders} "${PROJECT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    COMMENT "Embedding the compiled ubershader"
    VERBATIM)
add_custom_target(toki_ubershader DEPENDS "${shaderHeader}")
add_dependencies(toki_gfx toki_ubershader)

target_include_directories(toki_gfx PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_sources(toki_gfx PRIVATE
        BlendSetup.cpp
        Device.cpp
        DrawBatches.cpp
        ImageTexture.cpp
        PipelineCache.cpp
        RenderTarget.cpp
        Renderer.cpp
        ShaderCode.cpp
        ShaderUniforms.cpp)
