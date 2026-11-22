cmake_minimum_required(VERSION 3.21)
project(ButchiesUsbRecoveryStudioAI VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)
qt_standard_project_setup(REQUIRES 6.5)

set(CORE_HEADERS
    core/common.h core/disk_io.h core/catalog.h core/usb_inventory.h core/scsi_probe.h core/event_logs.h
    core/reader.h core/partition_fs.h core/fs_reader.h core/rescue_imager.h core/surface_scan.h
    core/file_carver.h core/repair_actions.h core/capacity_test.h core/knowledge_store.h
    core/diagnosis_engine.h core/agent.h)

qt_add_executable(usb-recovery-studio WIN32 main.cpp ${CORE_HEADERS})
qt_add_qml_module(usb-recovery-studio URI UsbRecovery VERSION 1.0
    QML_FILES Main.qml
    RESOURCES
        fonts/Inter-Regular.otf
        fonts/Inter-SemiBold.otf
        fonts/Inter-Bold.otf
        icons/box-amber.svg
        icons/box-blue.svg
        icons/box-green.svg
        icons/box-grey.svg
        icons/box-red.svg
        icons/box-white.svg
        icons/cache-amber.svg
        icons/cache-blue.svg
        icons/cache-green.svg
        icons/cache-grey.svg
        icons/cache-red.svg
        icons/cache-white.svg
        icons/check-amber.svg
        icons/check-blue.svg
        icons/check-green.svg
        icons/check-grey.svg
        icons/check-red.svg
        icons/check-white.svg
        icons/chip-amber.svg
        icons/chip-blue.svg
        icons/chip-green.svg
        icons/chip-grey.svg
        icons/chip-red.svg
        icons/chip-white.svg
        icons/clock-amber.svg
        icons/clock-blue.svg
        icons/clock-green.svg
        icons/clock-grey.svg
        icons/clock-red.svg
        icons/clock-white.svg
        icons/folder-amber.svg
        icons/folder-blue.svg
        icons/folder-green.svg
        icons/folder-grey.svg
        icons/folder-red.svg
        icons/folder-white.svg
        icons/home-amber.svg
        icons/home-blue.svg
        icons/home-green.svg
        icons/home-grey.svg
        icons/home-red.svg
        icons/home-white.svg
        icons/logo.svg
        icons/refresh-amber.svg
        icons/refresh-blue.svg
        icons/refresh-green.svg
        icons/refresh-grey.svg
        icons/refresh-red.svg
        icons/refresh-white.svg
        icons/save-amber.svg
        icons/save-blue.svg
        icons/save-green.svg
        icons/save-grey.svg
        icons/save-red.svg
        icons/save-white.svg
        icons/spark-amber.svg
        icons/spark-blue.svg
        icons/spark-green.svg
        icons/spark-grey.svg
        icons/spark-red.svg
        icons/spark-white.svg
        icons/tool-amber.svg
        icons/tool-blue.svg
        icons/tool-green.svg
        icons/tool-grey.svg
        icons/tool-red.svg
        icons/tool-white.svg
        icons/usb-amber.svg
        icons/usb-blue.svg
        icons/usb-green.svg
        icons/usb-grey.svg
        icons/usb-red.svg
        icons/usb-white.svg
        icons/warn-amber.svg
        icons/warn-blue.svg
        icons/warn-green.svg
        icons/warn-grey.svg
        icons/warn-red.svg
        icons/warn-white.svg
        icons/x-amber.svg
        icons/x-blue.svg
        icons/x-green.svg
        icons/x-grey.svg
        icons/x-red.svg
        icons/x-white.svg)
target_include_directories(usb-recovery-studio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(usb-recovery-studio PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Network)
set_target_properties(usb-recovery-studio PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME "Butchies-USB-Recovery-Studio-AI")

if(WIN32)
    target_compile_definitions(usb-recovery-studio PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
    target_sources(usb-recovery-studio PRIVATE app/app.rc)
    # Geräteerkennung (SetupAPI/CfgMgr), Ereignisanzeige (wevtapi), Registry
    target_link_libraries(usb-recovery-studio PRIVATE setupapi cfgmgr32 wevtapi advapi32)
endif()
if(MSVC)
    target_compile_options(usb-recovery-studio PRIVATE /utf-8 /W3)
    # Rohzugriff auf \\.\PhysicalDriveN braucht Administratorrechte – Windows fragt einmal per UAC.
    set_property(TARGET usb-recovery-studio APPEND_STRING PROPERTY
        LINK_FLAGS " /MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()
