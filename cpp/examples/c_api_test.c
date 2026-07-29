#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
    #include <windows.h>
    #define sleep(x) Sleep((x) * 1000)
    #define usleep(x) Sleep((x) / 1000)
#else
    #include <unistd.h>
#endif
#include <makxd_c.h>

void mouse_button_callback(makxd_mouse_button_t button, bool pressed, void* user_data) {
    const char* button_name = makxd_mouse_button_to_string(button);
    const char* action = pressed ? "pressed" : "released";
    printf("Mouse button callback: %s %s\n", button_name, action);
}

void connection_callback(bool connected, void* user_data) {
    printf("Connection callback: %s\n", connected ? "connected" : "disconnected");
}

int main() {
    printf("MAKXD C API Test\n");
    printf("================\n\n");
    
    // Test error string function
    printf("Testing error strings:\n");
    for (int i = 0; i <= 6; i++) {
        printf("  Error %d: %s\n", i, makxd_error_string((makxd_error_t)i));
    }
    printf("\n");
    
    // Create device
    printf("Creating device...\n");
    makxd_device_t* device = makxd_device_create();
    if (!device) {
        printf("Failed to create device\n");
        return 1;
    }
    printf("Device created successfully\n\n");
    
    // Test device discovery
    printf("Discovering devices...\n");
    makxd_device_info_t devices[10];
    int device_count = makxd_find_devices(devices, 10);
    printf("Found %d devices:\n", device_count);
    
    for (int i = 0; i < device_count; i++) {
        printf("  Device %d: %s (%s) VID:0x%04X PID:0x%04X %s\n", 
               i, devices[i].port, devices[i].description, 
               devices[i].vid, devices[i].pid,
               devices[i].is_connected ? "connected" : "disconnected");
    }
    printf("\n");
    
    // Test finding first device
    printf("Finding first device...\n");
    char first_port[256];
    makxd_error_t error = makxd_find_first_device(first_port, sizeof(first_port));
    if (error == MAKXD_SUCCESS) {
        printf("First device port: %s\n", first_port);
    } else {
        printf("No devices found or error: %s\n", makxd_error_string(error));
    }
    printf("\n");
    
    // Test connection status before connecting
    printf("Testing connection status before connecting...\n");
    bool connected = makxd_is_connected(device);
    makxd_connection_status_t status = makxd_get_status(device);
    printf("Connected: %s, Status: %d\n", connected ? "true" : "false", status);
    printf("\n");
    
    // Set up callbacks
    printf("Setting up callbacks...\n");
    error = makxd_set_mouse_button_callback(device, mouse_button_callback, NULL);
    if (error != MAKXD_SUCCESS) {
        printf("Failed to set mouse button callback: %s\n", makxd_error_string(error));
    } else {
        printf("Mouse button callback set successfully\n");
    }
    
    error = makxd_set_connection_callback(device, connection_callback, NULL);
    if (error != MAKXD_SUCCESS) {
        printf("Failed to set connection callback: %s\n", makxd_error_string(error));
    } else {
        printf("Connection callback set successfully\n");
    }
    printf("\n");
    
    // Test utility functions
    printf("Testing utility functions...\n");
    printf("Mouse button strings:\n");
    for (int i = 0; i <= 4; i++) {
        const char* name = makxd_mouse_button_to_string((makxd_mouse_button_t)i);
        makxd_mouse_button_t back = makxd_string_to_mouse_button(name);
        printf("  Button %d: %s (converts back to %d)\n", i, name, back);
    }
    printf("\n");
    
    // Test performance profiler
    printf("Testing performance profiler...\n");
    makxd_profiler_enable(true);
    makxd_profiler_reset_stats();
    printf("Performance profiler enabled and stats reset\n");
    printf("\n");
    
    // Attempt connection (this will likely fail without hardware)
    printf("Attempting to connect to device...\n");
    error = makxd_connect(device, ""); // Empty string to auto-detect
    if (error == MAKXD_SUCCESS) {
        printf("Connected successfully!\n");
        
        // Test device info
        makxd_device_info_t info;
        error = makxd_get_device_info(device, &info);
        if (error == MAKXD_SUCCESS) {
            printf("Device info: %s (%s) VID:0x%04X PID:0x%04X\n", 
                   info.port, info.description, info.vid, info.pid);
        }
        
        // Test version
        char version[128];
        error = makxd_get_version(device, version, sizeof(version));
        if (error == MAKXD_SUCCESS) {
            printf("Device version: %s\n", version);
        }
        
        // Test some basic functionality
        printf("\nTesting basic mouse operations...\n");
        
        // Test mouse movement
        printf("Testing mouse movement...\n");
        error = makxd_mouse_move(device, 10, 10);
        printf("Mouse move result: %s\n", makxd_error_string(error));
        
        // Test mouse click
        printf("Testing mouse click...\n");
        error = makxd_mouse_click(device, MAKXD_MOUSE_LEFT);
        printf("Mouse click result: %s\n", makxd_error_string(error));
        
        // Test smooth movement
        printf("Testing smooth mouse movement...\n");
        error = makxd_mouse_move_smooth(device, -10, -10, 5);
        printf("Smooth move result: %s\n", makxd_error_string(error));
        
        // Test mouse wheel
        printf("Testing mouse wheel...\n");
        error = makxd_mouse_wheel(device, 1);
        printf("Mouse wheel result: %s\n", makxd_error_string(error));
        
        // Test performance mode
        printf("Testing performance mode...\n");
        error = makxd_enable_high_performance_mode(device, true);
        printf("Enable performance mode result: %s\n", makxd_error_string(error));
        
        bool perf_enabled;
        error = makxd_is_high_performance_mode_enabled(device, &perf_enabled);
        if (error == MAKXD_SUCCESS) {
            printf("Performance mode enabled: %s\n", perf_enabled ? "true" : "false");
        }
        
        // Test batch operations
        printf("Testing batch operations...\n");
        makxd_batch_builder_t* batch = makxd_create_batch(device);
        if (batch) {
            printf("Batch created successfully\n");
            
            // Add some operations to the batch
            makxd_batch_move(batch, 5, 5);
            makxd_batch_click(batch, MAKXD_MOUSE_LEFT);
            makxd_batch_move(batch, -5, -5);
            
            // Execute the batch
            error = makxd_batch_execute(batch);
            printf("Batch execution result: %s\n", makxd_error_string(error));
            
            makxd_batch_destroy(batch);
            printf("Batch destroyed\n");
        } else {
            printf("Failed to create batch\n");
        }
        
        // Test click sequence
        printf("Testing click sequence...\n");
        makxd_mouse_button_t sequence[] = {MAKXD_MOUSE_LEFT, MAKXD_MOUSE_RIGHT, MAKXD_MOUSE_LEFT};
        error = makxd_click_sequence(device, sequence, 3, 100);
        printf("Click sequence result: %s\n", makxd_error_string(error));
        
        // Test move pattern
        printf("Testing move pattern...\n");
        makxd_point_t pattern[] = {{10, 0}, {0, 10}, {-10, 0}, {0, -10}};
        error = makxd_move_pattern(device, pattern, 4, true, 5);
        printf("Move pattern result: %s\n", makxd_error_string(error));
        
        printf("\nConnection established, device is functional!\n");
        
    } else {
        printf("Connection failed: %s\n", makxd_error_string(error));
        printf("This is expected if no MAKXD device is connected.\n");
    }
    
    // Test some operations that work without connection
    printf("\nTesting operations that work without connection...\n");
    
    // Test lock state queries (should work but may return default values)
    bool x_locked;
    error = makxd_is_mouse_x_locked(device, &x_locked);
    printf("X locked query result: %s (locked: %s)\n", 
           makxd_error_string(error), x_locked ? "true" : "false");
    
    // Test profiler stats
    printf("\nGetting performance stats...\n");
    makxd_perf_stat_t stats[10];
    int stat_count = makxd_profiler_get_stats(stats, 10);
    printf("Got %d performance stats:\n", stat_count);
    for (int i = 0; i < stat_count; i++) {
        printf("  %s: %llu calls, %llu μs total\n", 
               stats[i].command_name, 
               (unsigned long long)stats[i].call_count,
               (unsigned long long)stats[i].total_microseconds);
    }
    
    // Clean up
    printf("\nCleaning up...\n");
    makxd_disconnect(device);
    makxd_device_destroy(device);
    printf("Device destroyed\n");
    
    printf("\nC API test completed successfully!\n");
    return 0;
}