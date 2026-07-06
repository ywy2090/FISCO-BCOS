# Deep recursive CALL fixtures (Call1024*, LoopCallsDepth*) need >8 MiB thread stack on
# macOS/Linux default; host enforces depth 1024 but each frame still consumes C++ stack.
function(bcos_evm_enable_deep_call_stack target)
    if(APPLE)
        # ld64 -stack_size is hexadecimal bytes (32 MiB).
        target_link_options(${target} PRIVATE "-Wl,-stack_size,0x2000000")
    elseif(UNIX)
        target_link_options(${target} PRIVATE "-Wl,-z,stack-size=33554432")
    endif()
endfunction()
