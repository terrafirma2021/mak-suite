#include <makxd.h>
#include <iostream>

int main() {
    try {
        auto devices = makxd::Device::findDevices();
        if (devices.empty()) {
            std::cout << "No MAKXD devices found.\n";
            return 1;
        }

        makxd::Device device;
        if (!device.connect(devices[0].port)) {
            std::cout << "Failed to connect to device.\n";
            return 1;
        }

        std::cout << "Connected to device: " << devices[0].port << "\n";

        const auto route = device.device();
        if (!route) {
            std::cout << "Device route query failed\n";
            return 1;
        }

        // Basic mouse operations
        if (!device.mouseMove(100, 0)) {
            std::cout << "mouseMove(100,0) failed\n";
            return 1;
        }
        if (!device.mouseMove(-100, 0)) {
            std::cout << "mouseMove(-100,0) failed\n";
            return 1;
        }
        if (!device.click(makxd::MouseButton::LEFT)) {
            std::cout << "click(LEFT) failed\n";
            return 1;
        }
        if (!device.mouseWheel(3)) {
            std::cout << "mouseWheel(3) failed\n";
            return 1;
        }
        if (!device.mouseWheel(-3)) {
            std::cout << "mouseWheel(-3) failed\n";
            return 1;
        }

        if (!device.keyboardPress(std::string("A"))) {
            std::cout << "keyboardPress(A) failed\n";
            return 1;
        }

        if (!device.controllerControl(makxd::ControllerControl::SOUTH, 1)) {
            std::cout << "controllerControl(South,1) failed\n";
            return 1;
        }
        if (!device.controllerControl(makxd::ControllerControl::SOUTH, 0)) {
            std::cout << "controllerControl(South,0) failed\n";
            return 1;
        }

        device.disconnect();
        std::cout << "Demo completed successfully!\n";
    }
    catch (const makxd::MakxdException& e) {
        std::cerr << "MAKXD Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
