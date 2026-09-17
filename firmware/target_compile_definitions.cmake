if(NOT DEFINED PIKOCODE_TARGET)
    set(PIKOCODE_TARGET ${PROJECT_NAME})
endif()

target_compile_definitions(${PIKOCODE_TARGET} PRIVATE
    # GPIO 23 is driven high for the SMPS, so the WS2812 output is off.
    WS2812_ENABLED=0
    PCB_V2_LAYOUT=0
)
