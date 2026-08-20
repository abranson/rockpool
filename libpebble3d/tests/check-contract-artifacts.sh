#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Validate the public org.rockpool introspection and platform ABI artifacts.

set -eu

program=${0##*/}
require_committed=false

fail()
{
    printf '%s: %s\n' "$program" "$*" >&2
    exit 1
}

case ${1-} in
    '')
        ;;
    --require-committed)
        require_committed=true
        ;;
    *)
        fail "usage: $program [--require-committed]"
        ;;
esac

require_committed_file()
{
    file=$1
    description=$2
    file_directory=$(CDPATH= cd "$(dirname "$file")" && pwd -P) || \
        fail "cannot resolve directory for $description: $file"
    canonical_file=$file_directory/$(basename "$file")
    repository=$(git -C "$file_directory" rev-parse --show-toplevel 2>/dev/null) || \
        fail "cannot find Git repository for $description: $file"
    repository=$(CDPATH= cd "$repository" && pwd -P) || \
        fail "cannot resolve Git repository for $description: $file"

    case $canonical_file in
        "$repository"/*)
            relative_file=${canonical_file#"$repository"/}
            ;;
        *)
            fail "required $description is outside its Git repository: $file"
            ;;
    esac

    if ! git -C "$repository" ls-files --error-unmatch -- "$relative_file" \
        >/dev/null 2>&1
    then
        fail "untracked $description: $file"
    fi

    if ! git -C "$repository" diff --no-ext-diff --quiet HEAD -- "$relative_file"
    then
        fail "uncommitted $description: $file (commit it before creating a release source archive)"
    fi
}

require_file()
{
    if [ ! -r "$1" ]; then
        fail "missing readable $2: $1"
    fi

    if [ "$require_committed" = true ]; then
        require_committed_file "$1" "$2"
    fi
}

require_fixed()
{
    needle=$1
    file=$2
    description=$3

    if command -v rg >/dev/null 2>&1; then
        if rg --fixed-strings --quiet -- "$needle" "$file"; then
            return 0
        fi
    elif command -v grep >/dev/null 2>&1; then
        if grep -F -q -- "$needle" "$file"; then
            return 0
        fi
    else
        fail "requires rg or grep to inspect contract artifacts"
    fi

    fail "missing $description in $file"
}

reject_extended()
{
    pattern=$1
    file=$2
    description=$3

    if command -v rg >/dev/null 2>&1; then
        if rg --quiet -- "$pattern" "$file"; then
            fail "$description in $file"
        fi
    elif command -v grep >/dev/null 2>&1; then
        if grep -E -q "$pattern" "$file"; then
            fail "$description in $file"
        fi
    else
        fail "requires rg or grep to inspect contract artifacts"
    fi
}

reject_tree_extended()
{
    pattern=$1
    description=$2
    shift 2

    if command -v rg >/dev/null 2>&1; then
        if rg --ignore-case --quiet -- "$pattern" "$@"; then
            fail "$description"
        fi
    elif command -v grep >/dev/null 2>&1; then
        if grep -R -E -i -q -- "$pattern" "$@"; then
            fail "$description"
        fi
    else
        fail "requires rg or grep to inspect source trees"
    fi
}

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd -P) || \
    fail "cannot determine script directory"
libpebble3d_dir=$(CDPATH= cd "$script_dir/.." && pwd -P) || \
    fail "cannot determine libpebble3d directory"
project_dir=$(CDPATH= cd "$libpebble3d_dir/.." && pwd -P) || \
    fail "cannot determine project directory"
contract_checker=$script_dir/check-contract-artifacts.sh
source_archive_script=$project_dir/rpm/create-source-archive.sh
gitmodules=$project_dir/.gitmodules
xml=$libpebble3d_dir/api/org.rockpool.xml
header=$libpebble3d_dir/include/libpebble3d-platform.h
launcher_header=$libpebble3d_dir/include/libpebble3d-launcher-wire.h
loader=$libpebble3d_dir/native/platform_loader.c
platform_loader_event_test=$libpebble3d_dir/tests/platform_loader_event_test.c
rfcomm_socket=$libpebble3d_dir/native/rfcomm_socket.c
rfcomm_socket_test=$libpebble3d_dir/tests/rfcomm_socket_test.c
native_build=$libpebble3d_dir/build-native.sh
daemon_build=$libpebble3d_dir/build.sh
daemon_package=$libpebble3d_dir/package.sh
builder_dockerfile=$libpebble3d_dir/Dockerfile
primary_service=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/RockpoolService.kt
dbus_namespace_isolation_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/DBusNamespaceIsolationTest.kt
managed_object_publication=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/ManagedObjectPublication.kt
managed_object_publication_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/ManagedObjectPublicationTest.kt
account_settings_coordinator=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/AccountSettingsCoordinator.kt
account_settings_coordinator_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/AccountSettingsCoordinatorTest.kt
account_sync_coordinator=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/AccountSyncCoordinator.kt
account_sync_coordinator_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/AccountSyncCoordinatorTest.kt
account_locker_upgrade=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/AccountLockerUpgrade.kt
account_locker_upgrade_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/AccountLockerUpgradeTest.kt
account_locker_upgrade_reconciler=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/AccountLockerUpgradeReconciler.kt
account_locker_upgrade_reconciler_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/AccountLockerUpgradeReconcilerTest.kt
account_token_mutations=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/AccountTokenMutations.kt
account_token_mutations_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/AccountTokenMutationsTest.kt
health_settings_coordinator=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/HealthSettingsCoordinator.kt
health_settings_coordinator_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/HealthSettingsCoordinatorTest.kt
config_mutation_coordinator=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/LibPebbleConfigMutationCoordinator.kt
config_mutation_coordinator_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/LibPebbleConfigMutationCoordinatorTest.kt
notification_filter_coordinator=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/NotificationFilterCoordinator.kt
notification_filter_coordinator_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/NotificationFilterCoordinatorTest.kt
jvm_config_storage_policy=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/JvmConfigStoragePolicy.kt
jvm_config_storage_policy_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/JvmConfigStoragePolicyTest.kt
primary_settings_mappings=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/PrimarySettingsMappings.kt
primary_settings_mappings_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/PrimarySettingsMappingsTest.kt
primary_canned_reconciler=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/PrimaryCannedResponsesReconciler.kt
primary_canned_reconciler_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/PrimaryCannedResponsesReconcilerTest.kt
primary_watch_capabilities_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/PrimaryWatchCapabilitiesTest.kt
primary_applications_invalidation_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/RockpoolApplicationsInvalidationContractTest.kt
platform_provider_controller=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/PlatformProviderController.kt
platform_provider_controller_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/PlatformProviderControllerTest.kt
primary_connection_attempt_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/PrimaryPairAttemptRegistryTest.kt
connection_attempt_completion_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/ConnectionAttemptCompletionTest.kt
primary_discovery_mappings=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/PrimaryDiscoveryMappings.kt
primary_discovery_mappings_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/PrimaryDiscoveryMappingsTest.kt
bond_import_operation=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/BondedWatchImportOperation.kt
bond_forget_coordinator=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/BondedWatchForgetCoordinator.kt
bond_forget_coordinator_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/BondedWatchForgetCoordinatorTest.kt
legacy_importer=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/LegacyRockpooldImporter.kt
legacy_importer_account_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/LegacyRockpooldImporterAccountTest.kt
legacy_importer_timeline_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/LegacyRockpooldImporterTimelineTest.kt
legacy_importer_weather_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/LegacyRockpooldImporterWeatherTest.kt
legacy_global_settings=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/LegacyGlobalSettingsReconciler.kt
legacy_global_settings_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/LegacyGlobalSettingsReconcilerTest.kt
rockpool_settings=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/RockpoolSettings.kt
rockpool_settings_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/RockpoolSettingsTest.kt
compat_mutation_signal_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockwork/RockworkMutationSignalTest.kt
compat_firmware_status_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockwork/RockworkFirmwareStatusTest.kt
compat_service=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockwork/RockworkService.kt
compat_pebble_object=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockwork/RockworkPebbleObject.kt
compat_health=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockwork/RockworkHealth.kt
compat_health_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockwork/RockworkHealthTest.kt
compat_notification_sources=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockwork/RockworkNotificationSources.kt
compat_notification_sources_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockwork/RockworkNotificationSourcesTest.kt
compat_notification_appearance=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockwork/RockworkNotificationAppearance.kt
compat_notification_mutations=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockwork/RockworkNotificationFilterMutations.kt
compat_weather=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockwork/RockworkWeather.kt
compat_weather_refresh=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockwork/RockworkWeatherAutoRefresh.kt
compat_weather_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockwork/RockworkWeatherTest.kt
compat_weather_refresh_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockwork/RockworkWeatherAutoRefreshTest.kt
proxy_project=$libpebble3d_dir/../platform-sailfish/proxy/proxy.pro
helper_project=$libpebble3d_dir/../platform-sailfish/helper/helper.pro
launcher_project=$libpebble3d_dir/../platform-sailfish/launcher/launcher.pro
proxy_source=$libpebble3d_dir/../platform-sailfish/proxy/sailfish_proxy.cpp
helper_source=$libpebble3d_dir/../platform-sailfish/helper/main.cpp
wire_header=$libpebble3d_dir/../platform-sailfish/common/wire.h
notification_monitor=$libpebble3d_dir/../platform-sailfish/helper/notificationmonitor.cpp
notification_monitor_header=$libpebble3d_dir/../platform-sailfish/helper/notificationmonitor.h
notification_monitor_test=$libpebble3d_dir/../platform-sailfish/tests/notificationmonitor_test.cpp
notification_monitor_test_project=$libpebble3d_dir/../platform-sailfish/tests/notificationmonitor_test.pro
call_monitor=$libpebble3d_dir/../platform-sailfish/helper/callmonitor.cpp
call_monitor_header=$libpebble3d_dir/../platform-sailfish/helper/callmonitor.h
main_volume_monitor=$libpebble3d_dir/../platform-sailfish/helper/mainvolumemonitor.cpp
main_volume_monitor_header=$libpebble3d_dir/../platform-sailfish/helper/mainvolumemonitor.h
call_monitor_test=$libpebble3d_dir/../platform-sailfish/tests/callmonitor_test.cpp
call_monitor_test_project=$libpebble3d_dir/../platform-sailfish/tests/callmonitor_test.pro
main_volume_monitor_test=$libpebble3d_dir/../platform-sailfish/tests/mainvolumemonitor_test.cpp
main_volume_monitor_test_project=$libpebble3d_dir/../platform-sailfish/tests/mainvolumemonitor_test.pro
stop_handshake_test=$libpebble3d_dir/../platform-sailfish/tests/stop_handshake_test.cpp
stop_handshake_test_project=$libpebble3d_dir/../platform-sailfish/tests/stop_handshake_test.pro
wire_test=$libpebble3d_dir/../platform-sailfish/tests/wire_test.cpp
wire_test_project=$libpebble3d_dir/../platform-sailfish/tests/wire_test.pro
launcher_source=$libpebble3d_dir/../platform-sailfish/launcher/main.c
service_dropin=$libpebble3d_dir/../platform-sailfish/libpebble3d-platform-sailfish.service.conf
package_spec=$libpebble3d_dir/rpm/libpebble3d.spec
rockpool_spec=$project_dir/rpm/rockpool.spec
rockwork_project=$project_dir/rockwork/rockwork.pro
rockpool_account=$project_dir/rockwork/rockpoolaccount.cpp
rockpool_account_header=$project_dir/rockwork/rockpoolaccount.h
rockpool_operation=$project_dir/rockwork/rockpooloperation.cpp
rockpool_operation_header=$project_dir/rockwork/rockpooloperation.h
rockpool_operation_test=$project_dir/rockwork/tests/rockpooloperation_test.cpp
rockpool_operation_project=$project_dir/rockwork/tests/rockpooloperation_test.pro
rockwork_pebble=$project_dir/rockwork/pebble.cpp
rockwork_pebble_header=$project_dir/rockwork/pebble.h
rockwork_pebbles=$project_dir/rockwork/pebbles.cpp
rockwork_pebbles_header=$project_dir/rockwork/pebbles.h
rockwork_pebbles_async_test=$project_dir/rockwork/tests/pebbles_async_test.cpp
rockwork_pebbles_async_project=$project_dir/rockwork/tests/pebbles_async_test.pro
rockwork_pebbles_async_runner=$project_dir/rockwork/tests/run-pebbles-async-test.sh
rockwork_pebble_async_test=$project_dir/rockwork/tests/pebble_async_test.cpp
rockwork_pebble_async_project=$project_dir/rockwork/tests/pebble_async_test.pro
rockwork_servicecontrol_async_test=$project_dir/rockwork/tests/servicecontrol_async_test.cpp
rockwork_servicecontrol_async_project=$project_dir/rockwork/tests/servicecontrol_async_test.pro
rockwork_screenshot_model=$project_dir/rockwork/screenshotmodel.cpp
rockwork_notification_model=$project_dir/rockwork/notificationsourcemodel.cpp
pair_watch_page=$project_dir/rockwork/qml/pages/PairWatchPage.qml
settings_page=$project_dir/rockwork/qml/pages/SettingsPage.qml
app_settings_page=$project_dir/rockwork/qml/pages/AppSettingsPage.qml
responses_page=$project_dir/rockwork/qml/pages/ResponsesPage.qml
send_text_settings_dialog=$project_dir/rockwork/qml/pages/SendTextSettingsDialog.qml
health_settings_dialog=$project_dir/rockwork/qml/pages/HealthSettingsDialog.qml
weather_settings_dialog=$project_dir/rockwork/qml/pages/WeatherSettingsDialog.qml
location_picker=$project_dir/rockwork/qml/pages/LocationPicker.qml
language_page=$project_dir/rockwork/qml/pages/LanguagePage.qml
developer_tools_page=$project_dir/rockwork/qml/pages/DeveloperToolsPage.qml
notifications_page=$project_dir/rockwork/qml/pages/NotificationsPage.qml
notification_color_page=$project_dir/rockwork/qml/pages/NotificationColorPage.qml
notification_icon_page=$project_dir/rockwork/qml/pages/NotificationIconPage.qml
installed_apps_page=$project_dir/rockwork/qml/pages/InstalledAppsPage.qml
installed_app_delegate=$project_dir/rockwork/qml/pages/InstalledAppDelegate.qml
app_upgrade_page=$project_dir/rockwork/qml/pages/AppUpgradePage.qml
app_store_details_page=$project_dir/rockwork/qml/pages/AppStoreDetailsPage.qml
import_package_page=$project_dir/rockwork/qml/pages/ImportPackagePage.qml
main_menu_page=$project_dir/rockwork/qml/pages/MainMenuPage.qml
screenshots_page=$project_dir/rockwork/qml/pages/ScreenshotsPage.qml
cover_page=$project_dir/rockwork/qml/cover/CoverPage.qml
rockpool_qml=$project_dir/rockwork/qml/rockpool.qml
service_control=$project_dir/rockwork/servicecontrol.cpp
service_control_header=$project_dir/rockwork/servicecontrol.h
daemon_main=$libpebble3d_dir/daemon/src/main/kotlin/main.kt
platform_notification_backend=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/PlatformNotificationBackend.kt
platform_calls_backend=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/PlatformCallsBackend.kt
platform_calls_backend_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/PlatformCallsBackendTest.kt
platform_volume_control=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/PlatformVolumeControl.kt
platform_volume_control_test=$libpebble3d_dir/daemon/src/test/kotlin/io/rebble/libpebblecommon/rockpool/PlatformVolumeControlTest.kt
platform_provider_module=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/PlatformTimeChanged.kt
sailfish_rfcomm_socket=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/SailfishRfcommSocket.kt
linux_notification_backend=$libpebble3d_dir/mobileapp/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/linux/notifications/LinuxNotificationBackend.kt
linux_notification_listener=$libpebble3d_dir/mobileapp/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/linux/notifications/LinuxNotificationListener.kt
linux_volume_control=$libpebble3d_dir/mobileapp/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/linux/music/VolumeControl.kt
linux_music_control=$libpebble3d_dir/mobileapp/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/linux/music/LinuxSystemMusicControl.kt
linux_pairing=$libpebble3d_dir/mobileapp/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/connection/bt/Pairing.jvm.kt
linux_bluez_manager=$libpebble3d_dir/mobileapp/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/connection/bt/ble/bluez/BluezManager.kt
linux_bonded_watch_seeder=$libpebble3d_dir/mobileapp/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/connection/BondedWatchSeeder.jvm.kt
linux_classic_connector=$libpebble3d_dir/mobileapp/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/connection/bt/classic/transport/JvmBtClassicConnector.kt
libpebble3_jvm_source=$libpebble3d_dir/mobileapp/libpebble3/src/jvmMain/kotlin
libpebble3_common_source=$libpebble3d_dir/mobileapp/libpebble3/src/commonMain/kotlin
libpebble3_health_dao=$libpebble3_common_source/io/rebble/libpebblecommon/database/dao/HealthSettingsRealDao.kt
libpebble3_health=$libpebble3_common_source/io/rebble/libpebblecommon/health/Health.kt
libpebble3_connection=$libpebble3_common_source/io/rebble/libpebblecommon/connection/LibPebble.kt
libpebble3_watch_manager=$libpebble3_common_source/io/rebble/libpebblecommon/connection/WatchManager.kt
libpebble3_config=$libpebble3_common_source/io/rebble/libpebblecommon/LibPebbleConfig.kt
libpebble3_notification_dao=$libpebble3_common_source/io/rebble/libpebblecommon/database/dao/NotificationAppDao.kt
libpebble3_notification_api=$libpebble3_common_source/io/rebble/libpebblecommon/notification/PlatformNotificationListener.kt
libpebble3_health_init_test=$libpebble3d_dir/mobileapp/libpebble3/src/jvmTest/kotlin/io/rebble/libpebblecommon/database/dao/HealthSettingsInitializationJvmTest.kt
libpebble3_config_test=$libpebble3d_dir/mobileapp/libpebble3/src/jvmTest/kotlin/io/rebble/libpebblecommon/LibPebbleConfigHolderJvmTest.kt
libpebble3_notification_dao_test=$libpebble3d_dir/mobileapp/libpebble3/src/jvmTest/kotlin/io/rebble/libpebblecommon/database/dao/NotificationAppForgettingJvmTest.kt
libpebble3_notification_api_test=$libpebble3d_dir/mobileapp/libpebble3/src/jvmTest/kotlin/io/rebble/libpebblecommon/notification/NotificationApiJvmTest.kt
libpebble3_watch_manager_test=$libpebble3d_dir/mobileapp/libpebble3/src/jvmTest/kotlin/io/rebble/libpebblecommon/connection/WatchManagerTest.kt
libpebble3_open_meteo=$libpebble3_jvm_source/io/rebble/libpebblecommon/linux/weather/OpenMeteoWeatherClient.kt
libpebble3_open_meteo_test=$libpebble3d_dir/mobileapp/libpebble3/src/jvmTest/kotlin/io/rebble/libpebblecommon/linux/weather/OpenMeteoWeatherClientJvmTest.kt
sailfish_linux_backend=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/rockpool/SailfishLinuxBackend.kt
sailfish_module_dir=$libpebble3d_dir/daemon/src/main/kotlin/io/rebble/libpebblecommon/sailfish
reflect_config=$libpebble3d_dir/reflect-config.json
proxy_config=$libpebble3d_dir/proxy-config.json
jni_config=$libpebble3d_dir/jni-config.json

require_mobileapp_gitlink()
{
    root_repository=$(git -C "$project_dir" rev-parse --show-toplevel 2>/dev/null) || \
        fail "cannot find Rockpool Git repository"
    root_repository=$(CDPATH= cd "$root_repository" && pwd -P) || \
        fail "cannot resolve Rockpool Git repository"
    mobileapp_path=libpebble3d/mobileapp

    if ! git -C "$root_repository" ls-files --error-unmatch -- "$mobileapp_path" \
        >/dev/null 2>&1
    then
        fail "untracked mobileapp submodule gitlink"
    fi
    if ! git -C "$root_repository" diff --no-ext-diff --quiet HEAD -- "$mobileapp_path"
    then
        fail "uncommitted mobileapp submodule gitlink"
    fi

    expected_mobileapp_commit=$(git -C "$root_repository" rev-parse \
        "HEAD:$mobileapp_path" 2>/dev/null) || \
        fail "cannot determine committed mobileapp submodule revision"
    actual_mobileapp_commit=$(git -C "$libpebble3d_dir/mobileapp" rev-parse HEAD 2>/dev/null) || \
        fail "cannot determine checked-out mobileapp revision"
    if [ "$actual_mobileapp_commit" != "$expected_mobileapp_commit" ]; then
        fail "mobileapp HEAD $actual_mobileapp_commit does not match committed gitlink $expected_mobileapp_commit"
    fi
}

require_release_trees_clean()
{
    if git -C "$project_dir" status --porcelain --untracked-files=all -- \
        .gitignore .gitmodules README.md \
        libpebble3d platform-sailfish rockpool.pro rockwork rpm | grep -q .
    then
        fail "Rockpool release source paths contain uncommitted or untracked content"
    fi
    if git -C "$libpebble3d_dir/mobileapp" status --porcelain --untracked-files=all | \
        grep -q .
    then
        fail "mobileapp release source tree contains uncommitted or untracked content"
    fi
}

require_file "$xml" "org.rockpool introspection XML"
require_file "$contract_checker" "release contract checker"
require_file "$source_archive_script" "committed release source-archive creator"
require_file "$gitmodules" "mobileapp submodule declaration"
require_file "$header" "platform ABI header"
require_file "$launcher_header" "private launcher control header"
require_file "$loader" "native platform loader"
require_file "$platform_loader_event_test" "native provider event/command regression"
require_file "$rfcomm_socket" "native RFCOMM socket bridge"
require_file "$rfcomm_socket_test" "native RFCOMM socket regression"
require_file "$native_build" "Native Image build script"
require_file "$daemon_build" "Native Image build driver"
require_file "$daemon_package" "daemon RPM packaging script"
require_file "$builder_dockerfile" "Native Image builder Dockerfile"
require_file "$primary_service" "primary D-Bus service"
require_file "$dbus_namespace_isolation_test" "D-Bus namespace-isolation regression"
require_file "$managed_object_publication" "primary ObjectManager publication gate"
require_file "$managed_object_publication_test" "primary ObjectManager publication regression test"
require_file "$account_settings_coordinator" "shared account settings coordinator"
require_file "$account_settings_coordinator_test" "shared account settings regressions"
require_file "$account_sync_coordinator" "primary account sync coordinator"
require_file "$account_sync_coordinator_test" "primary account sync regressions"
require_file "$account_locker_upgrade" "account-locker retirement gate"
require_file "$account_locker_upgrade_test" "account-locker retirement regressions"
require_file "$account_locker_upgrade_reconciler" "account-locker upgrade reconciler"
require_file "$account_locker_upgrade_reconciler_test" "account-locker upgrade retry regressions"
require_file "$account_token_mutations" "shared account-token mutation queue"
require_file "$account_token_mutations_test" "shared account-token ordering regressions"
require_file "$health_settings_coordinator" "shared health-settings coordinator"
require_file "$health_settings_coordinator_test" "shared health-settings regressions"
require_file "$config_mutation_coordinator" "shared LibPebble config mutation coordinator"
require_file "$config_mutation_coordinator_test" "shared LibPebble config mutation regressions"
require_file "$notification_filter_coordinator" "shared notification-filter coordinator"
require_file "$notification_filter_coordinator_test" "shared notification-filter regressions"
require_file "$jvm_config_storage_policy" "JVM config storage-capacity policy"
require_file "$jvm_config_storage_policy_test" "JVM config storage-capacity regressions"
require_file "$primary_settings_mappings" "primary global-settings mappings"
require_file "$primary_settings_mappings_test" "primary global-settings mapping regressions"
require_file "$primary_watch_capabilities_test" "primary watch-capability regressions"
require_file "$primary_applications_invalidation_test" "primary Applications invalidation regression"
require_file "$platform_provider_controller" "platform provider controller"
require_file "$platform_provider_controller_test" "platform provider controller regressions"
require_file "$primary_connection_attempt_test" "primary connection-attempt regressions"
require_file "$connection_attempt_completion_test" "primary connection completion regressions"
require_file "$bond_forget_coordinator_test" "bonded-watch Forget coordinator regressions"
require_file "$compat_pebble_object" "compatibility watch object"
require_file "$compat_health" "compatibility health record mapper"
require_file "$compat_health_test" "compatibility health record regressions"
require_file "$primary_discovery_mappings" "primary discovery capability mappings"
require_file "$primary_discovery_mappings_test" "primary discovery regressions"
require_file "$bond_import_operation" "bond import operation mapper"
require_file "$bond_forget_coordinator" "bonded-watch Forget coordinator"
require_file "$legacy_importer" "legacy-state importer"
require_file "$legacy_importer_account_test" "legacy account-state importer regressions"
require_file "$legacy_importer_timeline_test" "legacy-state importer retry regressions"
require_file "$legacy_importer_weather_test" "legacy weather-location importer regressions"
require_file "$legacy_global_settings" "legacy global-settings reconciler"
require_file "$legacy_global_settings_test" "legacy global-settings regressions"
require_file "$primary_canned_reconciler" "primary canned-response reconciler"
require_file "$primary_canned_reconciler_test" "primary canned-response reconciliation regressions"
require_file "$rockpool_settings" "Rockpool settings store"
require_file "$rockpool_settings_test" "Rockpool settings transaction regressions"
require_file "$compat_mutation_signal_test" "compatibility mutation regressions"
require_file "$compat_firmware_status_test" "compatibility firmware metadata regression"
require_file "$libpebble3_health_dao" "libpebble3 health settings DAO"
require_file "$libpebble3_health" "libpebble3 health service"
require_file "$libpebble3_connection" "libpebble3 concrete facade"
require_file "$libpebble3_config" "libpebble3 config holder"
require_file "$libpebble3_notification_dao" "libpebble3 notification application DAO"
require_file "$libpebble3_notification_api" "libpebble3 notification API"
require_file "$libpebble3_health_init_test" "libpebble3 health initialization regressions"
require_file "$libpebble3_config_test" "libpebble3 config-origin regressions"
require_file "$libpebble3_notification_dao_test" "libpebble3 notification DAO regressions"
require_file "$libpebble3_notification_api_test" "libpebble3 notification API regressions"
require_file "$compat_service" "compatibility D-Bus service"
require_file "$compat_notification_sources" "compatibility notification-source tracker"
require_file "$compat_notification_sources_test" "compatibility notification-source regressions"
require_file "$compat_notification_appearance" "compatibility notification-appearance coordinator"
require_file "$compat_notification_mutations" "compatibility notification-filter mutation queue"
require_file "$compat_weather" "compatibility weather coordinator"
require_file "$compat_weather_refresh" "supported automatic weather refresh"
require_file "$compat_weather_test" "compatibility weather regressions"
require_file "$compat_weather_refresh_test" "automatic weather refresh regressions"
require_file "$libpebble3_open_meteo" "native-Linux Open-Meteo client"
require_file "$libpebble3_open_meteo_test" "native-Linux Open-Meteo regressions"
require_file "$proxy_project" "Sailfish proxy project"
require_file "$helper_project" "Sailfish helper project"
require_file "$launcher_project" "Sailfish launcher project"
require_file "$proxy_source" "Sailfish proxy source"
require_file "$helper_source" "Sailfish helper source"
require_file "$wire_header" "Sailfish provider wire header"
require_file "$notification_monitor" "Sailfish notification monitor"
require_file "$notification_monitor_header" "Sailfish notification monitor header"
require_file "$notification_monitor_test" "Sailfish notification-reply regression"
require_file "$notification_monitor_test_project" "Sailfish notification-reply test project"
require_file "$call_monitor" "Sailfish calls monitor"
require_file "$call_monitor_header" "Sailfish calls monitor header"
require_file "$main_volume_monitor" "Sailfish system-volume monitor"
require_file "$main_volume_monitor_header" "Sailfish system-volume monitor header"
require_file "$call_monitor_test" "Sailfish calls-monitor regression"
require_file "$call_monitor_test_project" "Sailfish calls-monitor test project"
require_file "$main_volume_monitor_test" "Sailfish system-volume regression"
require_file "$main_volume_monitor_test_project" "Sailfish system-volume test project"
require_file "$stop_handshake_test" "Sailfish STOP_HOST regression"
require_file "$stop_handshake_test_project" "Sailfish STOP_HOST test project"
require_file "$wire_test" "Sailfish provider wire regression"
require_file "$wire_test_project" "Sailfish provider wire test project"
require_file "$launcher_source" "Sailfish launcher source"
require_file "$service_dropin" "Sailfish provider service drop-in"
require_file "$package_spec" "libpebble3d package spec"
require_file "$rockpool_spec" "Rockpool package spec"
require_file "$rockwork_project" "Rockwork qmake project"
require_file "$rockpool_account" "Rockpool Account1 client"
require_file "$rockpool_account_header" "Rockpool Account1 client header"
require_file "$rockpool_operation" "Rockpool Operation1 watcher"
require_file "$rockpool_operation_header" "Rockpool Operation1 watcher header"
require_file "$rockpool_operation_test" "Rockpool Operation1 watcher regression test"
require_file "$rockpool_operation_project" "Rockpool Operation1 watcher test project"
require_file "$rockwork_pebble" "Rockwork Pebble implementation"
require_file "$rockwork_pebble_header" "Rockwork Pebble header"
require_file "$rockwork_pebbles" "Rockwork watch-list model"
require_file "$rockwork_pebbles_header" "Rockwork watch-list model header"
require_file "$rockwork_pebbles_async_test" "Rockwork asynchronous manager regression test"
require_file "$rockwork_pebbles_async_project" "Rockwork asynchronous manager test project"
require_file "$rockwork_pebbles_async_runner" "Rockwork private-bus test runner"
require_file "$rockwork_pebble_async_test" "Rockwork asynchronous watch regression test"
require_file "$rockwork_pebble_async_project" "Rockwork asynchronous watch test project"
require_file "$rockwork_servicecontrol_async_test" "Rockwork asynchronous service-control regression test"
require_file "$rockwork_servicecontrol_async_project" "Rockwork asynchronous service-control test project"
require_file "$rockwork_screenshot_model" "Rockwork screenshot model"
require_file "$rockwork_notification_model" "Rockwork notification-source model"
require_file "$pair_watch_page" "Rockwork pairing page"
require_file "$settings_page" "Rockwork settings page"
require_file "$app_settings_page" "Rockwork application/OAuth settings page"
require_file "$responses_page" "Rockwork canned-response editor"
require_file "$send_text_settings_dialog" "Rockwork Send Text settings dialog"
require_file "$health_settings_dialog" "Rockwork Health settings dialog"
require_file "$weather_settings_dialog" "Rockwork weather settings dialog"
require_file "$location_picker" "Rockwork weather location picker"
require_file "$language_page" "Rockwork language settings page"
require_file "$developer_tools_page" "Rockwork developer-tools page"
require_file "$notifications_page" "Rockwork notifications page"
require_file "$notification_color_page" "Rockwork notification-colour page"
require_file "$notification_icon_page" "Rockwork notification-icon page"
require_file "$installed_apps_page" "Rockwork installed-apps page"
require_file "$installed_app_delegate" "Rockwork installed-app delegate"
require_file "$app_upgrade_page" "Rockwork app-upgrade page"
require_file "$app_store_details_page" "Rockwork app-store details page"
require_file "$import_package_page" "Rockwork package-import page"
require_file "$main_menu_page" "Rockwork main-menu page"
require_file "$screenshots_page" "Rockwork screenshots page"
require_file "$cover_page" "Rockwork cover page"
require_file "$rockpool_qml" "Rockwork application window"
require_file "$service_control" "Rockwork service controller"
require_file "$service_control_header" "Rockwork service-controller header"
require_file "$daemon_main" "daemon entrypoint"
require_file "$platform_notification_backend" "provider notification backend"
require_file "$platform_calls_backend" "provider calls backend"
require_file "$platform_calls_backend_test" "provider calls backend regressions"
require_file "$platform_volume_control" "provider system-volume control"
require_file "$platform_volume_control_test" "provider system-volume regressions"
require_file "$platform_provider_module" "provider override module"
require_file "$sailfish_rfcomm_socket" "Sailfish RFCOMM socket adapter"
require_file "$linux_notification_backend" "native-Linux notification backend"
require_file "$linux_notification_listener" "native-Linux notification listener"
require_file "$linux_volume_control" "native-Linux volume seam"
require_file "$linux_music_control" "native-Linux MPRIS music control"
require_file "$linux_pairing" "native-Linux pairing seam"
require_file "$linux_bluez_manager" "native-Linux BlueZ manager"
require_file "$linux_bonded_watch_seeder" "native-Linux bonded-watch planner"
require_file "$linux_classic_connector" "native-Linux Classic connector"
require_file "$sailfish_linux_backend" "Sailfish native-Linux overrides"
require_file "$reflect_config" "Native Image reflection configuration"
require_file "$proxy_config" "Native Image proxy configuration"
require_file "$jni_config" "Native Image JNI configuration"

if [ "$require_committed" = true ]; then
    require_mobileapp_gitlink
    require_release_trees_clean
fi

require_fixed 'sh "$checker" --require-committed' "$source_archive_script" \
    'committed-tree preflight before source archive creation'
require_fixed 'source revisions changed during release preflight' "$source_archive_script" \
    'captured source revisions around archive preflight'
require_fixed 'source revisions changed while creating the source archive' \
    "$source_archive_script" 'post-archive source revision validation'
require_fixed 'git -C "$project_dir" archive' "$source_archive_script" \
    'root committed-tree source archive'
require_fixed 'git -C "$mobileapp_dir" archive' "$source_archive_script" \
    'mobileapp committed-tree source archive'
require_fixed '--sort=name' "$source_archive_script" \
    'deterministic source archive member ordering'
require_fixed '--numeric-owner' "$source_archive_script" \
    'deterministic source archive ownership'
require_fixed 'chmod 0644 "$temporary_archive"' "$source_archive_script" \
    'world-readable published source archive'
require_fixed '--mtime="@$source_date_epoch"' "$source_archive_script" \
    'deterministic source archive timestamp'
require_fixed 'ln "$temporary_archive" "$archive_path"' "$source_archive_script" \
    'non-overwriting atomic source archive publication'
require_fixed 'OUT_TEMP=$(mktemp -d' "$daemon_build" \
    'fresh temporary Native Image output directory'
require_fixed '.build-provenance' "$daemon_build" \
    'Native Image output provenance marker'
require_fixed '--release)' "$daemon_build" \
    'explicit committed Native Image release-build mode'
require_fixed 'source_mode=committed' "$daemon_build" \
    'committed release-build provenance mode'
require_fixed 'git -C "$HERE/.." archive "$root_commit" |' "$daemon_build" \
    'immutable Rockpool release-build source snapshot'
require_fixed '--prefix=libpebble3d/mobileapp/' "$daemon_build" \
    'immutable mobileapp release-build source snapshot'
require_fixed '-v "$BUILD_HERE/daemon/build/jvmDist":/dist:ro' "$daemon_build" \
    'private read-only release JVM distribution input'
require_fixed '-v "$BUILD_HERE":/work:ro' "$daemon_build" \
    'private read-only release source input'
require_fixed 'artifact_sha256=%s %s' "$daemon_build" \
    'complete Native Image artifact inventory digests'
require_fixed 'builder_image_id=%s' "$daemon_build" \
    'immutable Native Image builder identity provenance'
require_fixed '"$builder_image_id" sh /work/build-native.sh' "$daemon_build" \
    'Native Image execution by immutable builder identity'
require_fixed 'sh "$BUILD_HERE/tests/check-contract-artifacts.sh"' "$daemon_build" \
    'Native Image validation of the captured release source snapshot'
require_fixed 'rev-parse "$root_commit:libpebble3d/mobileapp"' "$daemon_build" \
    'Native Image mobileapp revision from the captured Rockpool gitlink'
require_fixed 'PREVIOUS_OUT=' "$daemon_build" \
    'failed Native Image publication rollback state'
require_fixed 'mv "$OUT_TEMP" "$OUT"' "$daemon_build" \
    'fresh Native Image output publication'
require_fixed "'')" "$daemon_package" 'fresh-build default daemon packaging mode'
require_fixed 'sh "$checker" --require-committed' "$daemon_package" \
    'committed-tree daemon packaging preflight'
require_fixed '"$HERE/build.sh" --release' "$daemon_package" \
    'fresh Native Image build before default daemon packaging'
require_fixed '--reuse-current-build)' "$daemon_package" \
    'explicit current-build reuse packaging mode'
require_fixed 'libpebble3d-platform-loader.so' "$daemon_package" \
    'complete daemon build reuse validation'
require_fixed '.build-provenance' "$daemon_package" \
    'current-source daemon build reuse validation'
require_fixed 'source_mode=committed' "$daemon_package" \
    'committed release-build manifest validation'
require_fixed 'artifact_sha256=' "$daemon_package" \
    'complete build-manifest inventory validation'
require_fixed 'docker image inspect "$builder_image_id"' "$daemon_package" \
    'reusable Native Image builder identity validation'
require_fixed 'package_input=$(mktemp -d' "$daemon_package" \
    'verified daemon artifact input snapshot'
require_fixed 'require_current_build "$package_input"' "$daemon_package" \
    'post-copy daemon artifact snapshot verification'
require_fixed 'git -C "$HERE/.." archive "$root_commit" libpebble3d/rpm' \
    "$daemon_package" 'immutable committed daemon RPM metadata snapshot'
require_fixed '-v "$package_rpm":/rpm:ro' "$daemon_package" \
    'committed RPM metadata snapshot mount'
require_fixed '"$builder_image_id" sh -ec' "$daemon_package" \
    'daemon packaging with the recorded immutable builder image'
require_fixed 'package_output=$(mktemp -d' "$daemon_package" \
    'temporary daemon RPM output directory'
require_fixed 'cp "$1" "$publication_temp"' "$daemon_package" \
    'user-owned RPM publication staging copy'
require_fixed 'ln "$publication_temp" "$published_rpm"' "$daemon_package" \
    'non-overwriting daemon RPM publication'

if command -v xmllint >/dev/null 2>&1; then
    if ! xmllint --noout "$xml"; then
        fail "malformed org.rockpool introspection XML"
    fi
fi

# Static nodes represent /org/rockpool and its manager, platform, and dynamic
# watch/operation collections.  Runtime watches and operations use UUID names
# below the latter two collections.
require_fixed '<node name="/org/rockpool">' "$xml" \
    'root object path /org/rockpool'
require_fixed '<node name="Manager">' "$xml" \
    'object path /org/rockpool/Manager'
require_fixed '<node name="Platform">' "$xml" \
    'object path /org/rockpool/Platform'
require_fixed '<node name="Watches">' "$xml" \
    'object path /org/rockpool/Watches'
require_fixed '<node name="Operations">' "$xml" \
    'object path /org/rockpool/Operations'

for interface in \
    org.freedesktop.DBus.ObjectManager \
    org.rockpool.Manager1 \
    org.rockpool.Discovery1 \
    org.rockpool.Account1 \
    org.rockpool.Platform1 \
    org.rockpool.Watch1 \
    org.rockpool.Firmware1 \
    org.rockpool.Applications1 \
    org.rockpool.Timeline1 \
    org.rockpool.Notifications1 \
    org.rockpool.Messaging1 \
    org.rockpool.Health1 \
    org.rockpool.Profiles1 \
    org.rockpool.Screenshots1 \
    org.rockpool.Logs1 \
    org.rockpool.Developer1 \
    org.rockpool.Operation1
do
    require_fixed "<interface name=\"$interface\">" "$xml" \
        "required interface $interface"
done

require_fixed 'lp3_platform_get_api' "$header" \
    'lp3_platform_get_api ABI entry point'
for typed_record in \
    'struct lp3_platform_event_v1' \
    'struct lp3_platform_provider_status_v1' \
    'struct lp3_platform_notification_v1' \
    'struct lp3_platform_notification_command_v1' \
    'struct lp3_platform_call_command_v1' \
    'struct lp3_platform_call_state_v1' \
    'struct lp3_platform_media_state_v1' \
    'struct lp3_platform_time_state_v1' \
    'notification_command' \
    'call_command' \
    'media_command' \
    'get_status'
do
    require_fixed "$typed_record" "$header" "typed platform ABI record $typed_record"
done

# The two well-known names need physically distinct dbus-java connections.
require_fixed 'withShared(false)' "$primary_service" \
    'isolated org.rockpool D-Bus connection'
require_fixed 'withShared(false)' "$compat_service" \
    'isolated org.rockwork compatibility connection'
require_fixed 'fun `well known names expose only objects from their physical connection`()' \
    "$dbus_namespace_isolation_test" 'runtime D-Bus namespace-isolation regression'
require_fixed 'assertNotEquals(primary.uniqueName, compatibility.uniqueName)' \
    "$dbus_namespace_isolation_test" 'distinct physical D-Bus connections assertion'
require_fixed 'client.probe(PRIMARY_NAME, COMPATIBILITY_PATH).Identity()' \
    "$dbus_namespace_isolation_test" 'org.rockpool cross-name rejection assertion'
require_fixed 'client.probe(COMPATIBILITY_NAME, PRIMARY_PATH).Identity()' \
    "$dbus_namespace_isolation_test" 'org.rockwork cross-name rejection assertion'
require_fixed 'assertFailsWith<UnknownObject>' "$dbus_namespace_isolation_test" \
    'exact D-Bus UnknownObject isolation failure'
require_fixed 'watchBusConnection' "$compat_service" \
    'compatibility session-bus recovery'
require_fixed 'exported.values.forEach { conn.exportObject' "$compat_service" \
    'compatibility-object re-export after reconnect'
require_fixed 'RockworkPebble$WeatherLocationsChanged' "$reflect_config" \
    'legacy weather-location signal reflection metadata'
for service in "$primary_service" "$compat_service"
do
    require_fixed 'connectionLock' "$service" \
        'atomic D-Bus connection-loss transition'
done

# A dynamic path is part of GetManagedObjects only after the object has been
# exported on the connection backing that snapshot. Removal is the exact
# inverse, and add/remove signals stay bound to that same connection.
require_fixed 'ManagedObjectPublisher<DBusConnection>' "$primary_service" \
    'primary dynamic-object publication gate'
require_fixed 'fun <T> publish(export: (C) -> Unit, makeVisible: () -> T)' \
    "$managed_object_publication" 'export-before-visible publication primitive'
require_fixed 'fun <T> remove(unexport: (C) -> Unit, hide: () -> T)' \
    "$managed_object_publication" 'unexport-before-hidden removal primitive'
require_fixed 'synchronized(objectsLock)' "$managed_object_publication" \
    'ObjectManager map lock in publication primitive'
require_fixed 'synchronized(connectionLock)' "$managed_object_publication" \
    'nested connection lock in publication primitive'
require_fixed 'publishPublicName: (C) -> Unit = {},' "$managed_object_publication" \
    'public-name publication callback after reconnect snapshot export'
require_fixed 'publishPublicName(connection)' "$managed_object_publication" \
    'public-name publication inside ObjectManager gate'
require_fixed 'publishPublicName = { it.requestBusName(BUS_NAME) }' "$primary_service" \
    'org.rockpool name publication through ObjectManager gate'
require_fixed 'managedObjects.publishConnectionAfterExport(' \
    "$primary_service" 'atomic reconnect ObjectManager snapshot publication'
if ! awk '
    /exportSnapshot\(connection\)/ { exported = NR }
    /publishPublicName\(connection\)/ { named = NR }
    /setConnection\(connection\)/ { published = NR }
    END { exit !(exported && named && published && exported < named && named < published) }
' "$managed_object_publication"
then
    fail "ObjectManager snapshot, public name and internal connection are not published in order in $managed_object_publication"
fi
require_fixed 'export = { it.exportObject(exported.path, exported.watch) }' \
    "$primary_service" 'watch export through publication gate'
require_fixed 'makeVisible = { watches[id] = exported }' "$primary_service" \
    'watch ObjectManager visibility through publication gate'
require_fixed 'export = { it.exportObject(path, operation) }' "$primary_service" \
    'operation export through publication gate'
require_fixed 'makeVisible = { operations[path] = operation }' "$primary_service" \
    'operation ObjectManager visibility through publication gate'
require_fixed 'emitAdded(publication.connection, path, operation.managedInterfaces())' \
    "$primary_service" 'operation add signal bound to publication connection'
reject_extended 'connection\?\.exportObject\((exported\.path|path, operation)' \
    "$primary_service" 'dynamic object exported outside publication gate'
for publication_regression in \
    'snapshot reader cannot observe logical paths before reconnect export completes' \
    'complete snapshot is exported before the public name and internal connection' \
    'failed public name publication leaves the connection unpublished' \
    'failed reconnect export leaves the connection unpublished' \
    'publish exports before a snapshot can observe the object' \
    'remove unexports before a snapshot can observe the object as hidden' \
    'connection failure detaches but preserves the logical mutation' \
    'disconnected mutation is retained for the reconnect snapshot'
do
    require_fixed "fun \`$publication_regression\`()" "$managed_object_publication_test" \
        "ObjectManager publication regression $publication_regression"
done

# Applications1 depends on mutable watch-model metadata even when the stable
# serial/address path is retained. Every watch refresh must invalidate that
# projection for signal-driven ObjectManager clients.
require_fixed 'propertiesChanged(APPLICATIONS_INTERFACE, setOf("Applications"))' \
    "$primary_service" 'stable-watch Applications1 invalidation'
require_fixed 'fun `watch refresh republishes Applications1 for a stable watch object`()' \
    "$primary_applications_invalidation_test" \
    'stable-watch Applications1 invalidation regression'

# Compatibility firmware properties form one visible tuple. A replacement
# candidate must invalidate the cache even while availability remains true.
require_fixed 'data class RockworkFirmwareStatus(' "$compat_service" \
    'complete compatibility firmware metadata status'
require_fixed 'fun shouldSignalAfter(previous: RockworkFirmwareStatus): Boolean = this != previous' \
    "$compat_service" 'compatibility firmware tuple change detector'
require_fixed 'firmwareStatus.shouldSignalAfter(watch.firmwareStatus)' "$compat_service" \
    'compatibility firmware metadata invalidation'
reject_extended 'var upgradeAvailable: Boolean' "$compat_service" \
    'lossy compatibility firmware availability-only cache'
require_fixed 'fun `firmware status invalidates every exposed metadata change`()' \
    "$compat_firmware_status_test" 'compatibility firmware metadata regression'

# Both account APIs receive their FIFO position synchronously at D-Bus method
# entry. This keeps compatibility dispatch and lazy primary Operation1 jobs in
# invocation order. Cancel still wins before beginCommit; committed persistence
# is non-cancellable.
require_fixed 'private val tokenMutations = AccountTokenMutations()' \
    "$account_settings_coordinator" 'coordinator-owned shared account-token queue'
require_fixed 'fun enqueueTokenMutation(token: String): AccountTokenMutation = tokenMutations.enqueue(token)' \
    "$account_settings_coordinator" 'shared account-token FIFO admission seam'
require_fixed 'val mutation = accountSettings.enqueueTokenMutation(token)' "$primary_service" \
    'synchronous primary account-token FIFO admission'
require_fixed 'val mutation = accountSettings.enqueueTokenMutation(token)' "$compat_pebble_object" \
    'synchronous compatibility account-token FIFO admission'
require_fixed '}.invokeOnCompletion { mutation.release() }' "$compat_pebble_object" \
    'compatibility account-token cancellation release'
require_fixed 'onCompletion = mutation::release' "$primary_service" \
    'pre-start account-operation cancellation release'
require_fixed 'mutation.execute(::beginCommit, accountSettings::setToken)' "$primary_service" \
    'account mutation commit-boundary execution'
require_fixed 'persist = accountSettings::setToken' "$compat_pebble_object" \
    'compatibility account mutation ordered persistence'
if ! awk '
    /override fun setOAuthToken\(token: String\)/ { in_setter = 1 }
    in_setter && /scope\.launch/ { async_dispatch = 1 }
    in_setter && /runBlocking/ { blocking_dispatch = 1 }
    in_setter && /override fun syncAppsFromCloud\(\)/ { in_setter = 0 }
    END { exit !(async_dispatch && !blocking_dispatch) }
' "$compat_pebble_object"; then
    fail "compatibility account-token setter must dispatch asynchronously in $compat_pebble_object"
fi
require_fixed 'prepared.invokeOnCompletion' "$primary_service" \
    'lazy operation completion hook'
require_fixed 'private var tail = CompletableDeferred(Unit)' "$account_token_mutations" \
    'account mutation FIFO tail'
require_fixed 'predecessor.await()' "$account_token_mutations" \
    'account mutation predecessor ordering'
require_fixed 'currentCoroutineContext().ensureActive()' "$account_token_mutations" \
    'account mutation cancellation check before commit'
require_fixed 'withContext(NonCancellable)' "$account_token_mutations" \
    'non-cancellable committed account mutation'
for account_regression in \
    'writes retain invocation order when the later operation starts first' \
    'pre-start cancellation releases its FIFO position' \
    'completion hook releases a mutation cancelled before its launch body starts' \
    'cancelling a queued operation cannot let its successor overtake the head' \
    'persistence failure advances the next write' \
    'persistence exception cannot strand the queue' \
    'cancel wins before commit without persisting and advances the queue' \
    'commit keeps successors blocked through non-cancellable persistence'
do
    require_fixed "fun \`$account_regression\`()" "$account_token_mutations_test" \
        "shared account-token regression $account_regression"
done
require_fixed 'fun `primary and compatibility admissions share one invocation-order queue`()' \
    "$account_settings_coordinator_test" 'cross-API account-token ordering regression'

# Notifications1 has a binary enabled field while the compatibility UI owns
# mode 1 (enabled except while the phone is active). An unchanged primary
# round-trip must preserve that stricter policy in every projection/runtime.
# The separate libpebble application mute row is durable policy too: await it
# under the same coordinator lock and repair every learned row after import,
# before the provider can deliver a notification.
require_fixed 'existing?.suppressWhenActive == true' "$notification_filter_coordinator" \
    'conditional compatibility notification policy preservation'
require_fixed 'put(watchPrefix, encodeNotificationFilters(watchPrefix, effective))' \
    "$notification_filter_coordinator" 'effective primary watch filter persistence'
require_fixed 'encodeCanonicalNotificationFilters(effective)' \
    "$notification_filter_coordinator" 'effective global filter persistence'
require_fixed 'GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING to "true"' \
    "$notification_filter_coordinator" 'durable explicit-empty notification policy marker'
require_fixed 'hasCanonicalNotificationFilters(canonical)' \
    "$notification_filter_coordinator" 'marker-aware canonical notification policy load'
require_fixed 'GLOBAL_NOTIFICATION_FILTER_CONFIGURED_SETTING' "$legacy_importer" \
    'legacy notification fallback suppression after canonical policy commit'
require_fixed 'GLOBAL_NOTIFICATION_RETIREMENT_PREFIX' "$notification_filter_coordinator" \
    'durable compatibility notification retirement intents'
require_fixed 'forgetApplication(source)' "$notification_filter_coordinator" \
    'awaited compatibility notification retirement'
require_fixed 'clearRetirements(retirements)' "$notification_filter_coordinator" \
    'startup retirement-intent replay and cleanup'
require_fixed 'withTimeoutOrNull(muteStateTimeoutMs)' "$notification_filter_coordinator" \
    'bounded notification application state and retirement handoff'
require_fixed 'commit(replacements, previous, effective)' "$notification_filter_coordinator" \
    'effective notification runtime handoff'
require_fixed 'private val updateMuteState: suspend (String, MuteState) -> Unit' \
    "$notification_filter_coordinator" 'awaited notification mute mutation'
require_fixed 'private val reconcileMuteStates: suspend (Map<String, MuteState>) -> Unit' \
    "$notification_filter_coordinator" 'complete notification mute reconciliation seam'
require_fixed 'suspend fun reconcilePersistedState(): Boolean' \
    "$notification_filter_coordinator" 'startup notification policy reconciliation'
require_fixed 'updateMuteState = concreteLibPebble::persistNotificationAppMuteState' \
    "$daemon_main" 'concrete awaited notification mute persistence wiring'
require_fixed 'reconcileMuteStates = concreteLibPebble::reconcileNotificationAppMuteStates' \
    "$daemon_main" 'complete notification mute reconciliation wiring'
require_fixed 'if (!notificationFilters.reconcilePersistedState())' "$daemon_main" \
    'initial notification reconciliation after legacy import'
reject_extended 'updateMuteState[[:space:]]*=[[:space:]]*libPebble::updateNotificationAppMuteState' \
    "$daemon_main" 'fire-and-forget notification mute persistence wiring'
if ! awk '
    /legacyImporter\.importIfNeeded\(libPebble\)/ && !imported { imported = NR }
    /notificationFilters\.reconcilePersistedState\(\)/ && !reconciled { reconciled = NR }
    /platformProvider\.start\(\)/ && !started { started = NR }
    END { exit !(imported && imported < reconciled && reconciled < started) }
' "$daemon_main"
then
    fail "notification policy is not reconciled after import and before provider start in $daemon_main"
fi
require_fixed 'suspend fun persistNotificationAppMuteState(' "$libpebble3_notification_api" \
    'awaited libpebble3 notification mute API'
require_fixed 'notificationAppDao.updateAppMuteState(packageName, muteState)' \
    "$libpebble3_notification_api" 'direct awaited notification mute DAO call'
require_fixed 'suspend fun reconcileAppMuteStates(muteStates: Map<String, MuteState>)' \
    "$libpebble3_notification_dao" 'transactional learned-application mute reconciliation'
require_fixed 'notificationApi.persistNotificationAppMuteState(packageName, muteState)' \
    "$libpebble3_connection" 'concrete awaited notification mute facade'
require_fixed 'notificationApi.reconcileNotificationAppMuteStates(muteStates)' \
    "$libpebble3_connection" 'concrete notification mute reconciliation facade'
require_fixed 'if (!result.applicationStateUpdated)' "$primary_service" \
    'primary operation reports pending notification application state'
require_fixed 'if (!result.runtimePolicyUpdated)' "$primary_service" \
    'primary operation reports pending provider notification policy'
for filter_regression in \
    'forget persists an empty canonical policy before retiring app and clears migration fallbacks' \
    'explicit empty canonical policy survives startup and suppresses watch fallback' \
    'failed application retirement remains durable and startup retries then clears it' \
    'startup retirement timeout keeps durable intent for retry' \
    're-enabling a source cancels its pending retirement' \
    'primary replacement and compatibility mutation cannot interleave' \
    'primary enabled replacement preserves only conditional suppression' \
    'primary disabled replacement changes conditional compatibility mode to disabled' \
    'primary enabled replacement keeps always enabled mode' \
    'primary replacement removes an omitted conditional source' \
    'a held mute update blocks a following mutation' \
    'startup reconciliation replays canonical filters and their mute states' \
    'startup reconciliation does not apply mute states when runtime replay fails'
do
    require_fixed "fun \`$filter_regression\`" "$notification_filter_coordinator_test" \
        "primary notification-filter regression $filter_regression"
done
require_fixed 'fun `does not recreate notification fallback after empty canonical policy`()' \
    "$legacy_importer_timeline_test" \
    'legacy notification fallback suppression regression'
require_fixed 'class RockworkNotificationSourcePublisher(' "$compat_notification_sources" \
    'single-owner compatibility notification-source publisher'
require_fixed 'Channel<Unit>(Channel.CONFLATED)' "$compat_notification_sources" \
    'conflated compatibility notification-source invalidation queue'
require_fixed 'notificationSourcePublisher.invalidate()' "$compat_service" \
    'ordered compatibility notification-source invalidation wiring'
require_fixed 'fun `publisher emits old then newest snapshot when its first emission is held`()' \
    "$compat_notification_sources_test" \
    'ordered compatibility notification-source publication regression'
require_fixed 'fun persistNotificationAppMuteStateDelegatesToAwaitedDaoMutation()' \
    "$libpebble3_notification_api_test" 'awaited notification mute API regression'
require_fixed 'fun reconcileNotificationAppMuteStatesDelegatesCompleteSnapshot()' \
    "$libpebble3_notification_api_test" 'complete notification mute API regression'
require_fixed 'fun reconciliationAppliesExplicitMuteAndResetsOmittedApplications()' \
    "$libpebble3_notification_dao_test" 'transactional notification mute DAO regression'

# Account sync reserves its account-global slot before starting libpebble3, but
# every start/await/cancel/account-change exit must retire that exact slot. A
# synchronous requestLockerSync exception must never wedge all later calls as
# Busy. Cross the commit boundary before start because the Deferred completes
# only after libpebble3 has durably applied the locker snapshot.
require_fixed 'private val accountSyncCoordinator = AccountSyncCoordinator' "$primary_service" \
    'primary account sync coordinator ownership'
require_fixed 'accountSyncCoordinator.accountChanged(change.generation)' "$primary_service" \
    'account replacement invalidates active primary sync'
require_fixed 'if (change.token.isNotBlank()) {' "$primary_service" \
    'signed-in account change starts best-effort locker synchronization'
require_fixed 'accountSyncCoordinator.synchronize(' "$primary_service" \
    'primary Account1 Sync coordinator dispatch'
require_fixed 'start = libPebble::requestLockerSync' "$primary_service" \
    'primary Account1 libpebble3 sync start seam'
reject_extended 'accountSyncGeneration|accountStateLock' "$primary_service" \
    'ad-hoc primary account sync reservation state'
require_fixed 'if (activeGeneration != null) return AccountSyncResult.Busy' \
    "$account_sync_coordinator" 'account sync duplicate gate'
require_fixed 'sync = try {' "$account_sync_coordinator" \
    'account sync start inside guarded cleanup path'
require_fixed 'activeGeneration = null' "$account_sync_coordinator" \
    'account sync reservation release'
require_fixed 'if (!beginCommit()) return AccountSyncResult.Cancelled' \
    "$account_sync_coordinator" 'account sync pre-start cancellation boundary'
if ! awk '
    /if \(!beginCommit\(\)\) return AccountSyncResult.Cancelled/ { commit = NR }
    /^[[:space:]]*sync = try \{/ { start = NR }
    END { exit !(commit && start && commit < start) }
' "$account_sync_coordinator"; then
    fail "Account1 Sync must commit before starting its durable locker mutation"
fi
for account_sync_regression in \
    'synchronous start failure releases the slot for a retry' \
    'duplicate active sync is busy and does not start twice' \
    'account replacement cancels old sync and permits the new account' \
    'account replacement during synchronous start rejects returned work' \
    'cancel before commit does not start locker sync' \
    'commit boundary is crossed before synchronous locker sync start' \
    'automatic login sync shares the explicit synchronization slot'
do
    require_fixed "fun \`$account_sync_regression\`()" "$account_sync_coordinator_test" \
        "primary account sync regression $account_sync_regression"
done

# Retiring pre-session locker state is only complete after the replacement
# account snapshot succeeds. A transient start/network failure must remain
# retryable in-process, while the periodic recovery pass must not cancel a
# still-running request and restart it forever.
require_fixed 'fun retryIfPending()' "$account_locker_upgrade_reconciler" \
    'account-locker upgrade retry entrypoint'
require_fixed 'suspend fun recoverIfPending(retireIfNeeded: suspend () -> Boolean)' \
    "$account_locker_upgrade_reconciler" 'failed retirement-marker recovery'
require_fixed 'internal fun isAccountLockerSessionGateRetired(marker: String): Boolean' \
    "$account_locker_upgrade" 'durable destructive-retirement classification'
require_fixed '(active != null || markerState() == ACCOUNT_LOCKER_SESSION_GATE_COMPLETE)' \
    "$account_locker_upgrade_reconciler" 'account-locker sync callback ownership'
require_fixed 'releaseFailedIfCurrent(expectedGeneration, token, sync)' \
    "$account_locker_upgrade_reconciler" 'failed account-locker sync release'
require_fixed 'isAccountLockerSessionGateRetired(' "$daemon_main" \
    'persisted retirement prevents destructive startup replay'
require_fixed 'accountLockerUpgradeReconciler.recoverIfPending {' "$daemon_main" \
    'periodic account-locker upgrade recovery wiring'
require_fixed 'fun `retired and complete markers prevent a repeated destructive transition`()' \
    "$account_locker_upgrade_test" 'destructive account-retirement replay regression'
for account_locker_regression in \
    'failed reconciliation retries and completes without a restart' \
    'periodic retry preserves an unfinished reconciliation' \
    'synchronous start failure remains retryable' \
    'failed retirement marker is retried before reconciliation' \
    'completed sync remains owned until its marker callback finishes'
do
    require_fixed "fun \`$account_locker_regression\`()" \
        "$account_locker_upgrade_reconciler_test" \
        "account-locker upgrade regression $account_locker_regression"
done

# Health settings are one libpebble3 account-global row. Primary and
# compatibility mutations must share one read/merge/write barrier, and every
# exported watch object must project and signal that same canonical value.
require_fixed 'val healthSettings = HealthSettingsCoordinator(' "$daemon_main" \
    'one process-wide health settings coordinator'
require_fixed 'persistHealthSettings = concreteLibPebble::persistHealthSettings' "$daemon_main" \
    'daemon durable health persistence seam'
require_fixed 'healthSettings.startObserving(' "$daemon_main" \
    'watch-origin health settings observer'
require_fixed 'private val healthSettings: HealthSettingsCoordinator' "$primary_service" \
    'primary shared health coordinator injection'
require_fixed 'private val healthSettings: HealthSettingsCoordinator' "$compat_service" \
    'compatibility shared health coordinator injection'
require_fixed 'healthSettings.updateWithResultSuspend(' "$primary_service" \
    'primary serialized health mutation'
require_fixed 'healthSettings.addListener { healthSettingsPropertiesChanged() }' \
    "$primary_service" 'primary global health property fanout'
require_fixed 'healthSettings.addListener(::healthSettingsChanged)' "$compat_service" \
    'compatibility cross-service health signal fanout'
require_fixed 'primaryHealthSettings(healthSettings.current())' "$primary_service" \
    'canonical primary health property projection'
require_fixed 'suspend fun updateWithResultSuspend(' "$health_settings_coordinator" \
    'shared suspend health mutation seam'
require_fixed 'persistHealthSettings(updated)' "$health_settings_coordinator" \
    'awaited durable health settings write'
require_fixed 'withTimeoutOrNull(healthWriteTimeout)' "$health_settings_coordinator" \
    'bounded durable health settings write'
require_fixed '.collect(::observed)' "$health_settings_coordinator" \
    'watch-origin health settings collection'
reject_extended 'private var pending: Pending|libPebble\.updateHealthSettings\(updated\)' \
    "$health_settings_coordinator" 'asynchronous daemon health persistence'
reject_extended 'importedHealthSettings|\$settingPrefix\.health\.' "$primary_service" \
    'per-watch primary projection of account-global health settings'
for health_regression in \
    "primary and compatibility updates preserve each other's fields" \
    'cancelled commit neither writes nor notifies' \
    'timed out durable write is cancelled before a later write starts' \
    'watch-origin settings change notifies listeners once'
do
    require_fixed "fun \`$health_regression\`()" "$health_settings_coordinator_test" \
        "shared health settings regression $health_regression"
done
require_fixed 'fun `primary health projection uses the canonical libpebble row`()' \
    "$primary_settings_mappings_test" 'canonical primary health mapping regression'
reject_extended '"other"[[:space:]]*->|HealthGender\.Other\.ordinal' "$compat_health" \
    'non-round-trippable compatibility health gender'
require_fixed 'mapOf("gender" to Variant("other"))' "$compat_health_test" \
    'compatibility Other-string gender rejection regression'
require_fixed 'mapOf("gender" to Variant(2))' "$compat_health_test" \
    'compatibility Other-ordinal gender rejection regression'

# The libpebble configuration is one synchronous whole-record store. Serialize
# every runtime read/copy/write, keep primary canned groups global, and fan out
# compatibility calendar changes to every primary Timeline1 object.
require_fixed 'synchronized(lock)' "$config_mutation_coordinator" \
    'serialized whole-config mutation'
require_fixed 'private val configMutations = LibPebbleConfigMutationCoordinator.forLibPebble(libPebble)' \
    "$primary_service" 'primary shared config mutation coordinator'
require_fixed 'LibPebbleConfigMutationCoordinator.forLibPebble(libPebble).mutate' \
    "$compat_pebble_object" 'compatibility shared config mutation coordinator'
require_fixed 'configMutations.addListener(::libPebbleConfigChanged)' "$primary_service" \
    'primary config change listener'
require_fixed 'val configStoragePolicy = JvmConfigStoragePolicy(' "$daemon_main" \
    'daemon JVM config storage policy'
require_fixed 'configFitsStorage = configStoragePolicy::canAdmitCannedResponses' "$daemon_main" \
    'future-safe canned-response capacity admission'
require_fixed 'configStoragePolicy::canPersist' "$daemon_main" \
    'compatibility config capacity admission'
require_fixed 'applyMandatoryDaemonConfigPolicy(configMutations, configStoragePolicy)' \
    "$daemon_main" 'guarded mandatory daemon config mutation'
reject_extended 'libPebble\.updateConfig' "$daemon_main" \
    'direct unguarded daemon config mutation'
require_fixed 'configMutations.mutate { current ->' "$daemon_main" \
    'serialized startup PPoG config mutation'
require_fixed 'if (!configFitsStorage(updated))' "$compat_pebble_object" \
    'calendar capacity check before canonical persistence'
require_fixed 'fun `calendar storage rejection changes neither canonical record nor active config`()' \
    "$compat_mutation_signal_test" 'calendar storage rejection regression'
for config_storage_regression in \
    'preferences capacity is inclusive' \
    'canned admission reserves every later daemon config variant' \
    'mandatory daemon policy rejection does not write'
do
    require_fixed "fun \`$config_storage_regression\`()" "$jvm_config_storage_policy_test" \
        "JVM config storage regression $config_storage_regression"
done
require_fixed 'internal const val PRIMARY_CANNED_PREFIX = "primary.messaging.canned."' \
    "$primary_settings_mappings" 'global primary canned-response storage'
require_fixed 'PRIMARY_CANNED_CONFIGURED_SETTING to "true"' "$primary_canned_reconciler" \
    'durable explicit-empty primary canned state'
require_fixed 'cannedResponsesReconciler.replace(values, cannedResponses, ::beginCommit)' \
    "$primary_service" 'capacity-checked primary canned-response commit'
require_fixed 'if (!configFitsStorage(projected))' "$primary_canned_reconciler" \
    'primary canned-response storage admission check under config lock'
require_fixed 'val cannedResponses = PrimaryCannedResponsesReconciler(' "$daemon_main" \
    'primary canned-response startup reconciler'
require_fixed 'serializedLength(config) <= maximumLength' "$jvm_config_storage_policy" \
    'exact JVM config-storage capacity gate'
require_fixed 'fun serializedConfigLength(config: LibPebbleConfig): Int' \
    "$libpebble3_connection" 'concrete config serialization-length seam'
require_fixed 'if (!cannedResponses.isComplete()) cannedResponses.reconcile()' "$daemon_main" \
    'primary canned-response retry loop'
require_fixed 'messagingPropertiesChanged()' "$primary_service" \
    'global primary canned-response property fanout'
require_fixed '"CalendarEnabled" to Variant(libPebble.config.value.watchConfig.calendarPins)' \
    "$primary_service" 'global primary calendar projection'
require_fixed 'timelinePropertiesChanged()' "$primary_service" \
    'cross-service primary calendar property fanout'
reject_extended '\$settingPrefix\.(canned|calendar\.enabled)' "$primary_service" \
    'per-watch projection of account-global canned/calendar configuration'
require_fixed 'fun `serialized mutations preserve fields changed by another caller`()' \
    "$config_mutation_coordinator_test" 'whole-config lost-update regression'
require_fixed 'fun `global primary canned records ignore obsolete per-watch groups`()' \
    "$primary_settings_mappings_test" 'global primary canned projection regression'
for canned_reconcile_regression in \
    'oversized replacement is rejected before commit or canonical persistence' \
    'oversized persisted canonical state is terminal and a valid replacement recovers' \
    'replacement commits canonical marker and projected values together' \
    'canonical groups replay into config without changing unrelated fields' \
    'marked empty canonical collection clears generated defaults' \
    'unmarked canonical groups from an earlier daemon are replayed' \
    'absent canonical collection is a no-op' \
    'failed projection remains pending and a retry converges'
do
    require_fixed "fun \`$canned_reconcile_regression\`()" \
        "$primary_canned_reconciler_test" \
        "primary canned-response regression $canned_reconcile_regression"
done
require_fixed 'fun serializedLengthUsesTheExactJvmPreferencesBoundary()' \
    "$libpebble3_config_test" 'exact JVM preferences capacity regression'

# Operation1.Cancel must win before an irreversible side effect or lose before
# it. It must never report cancelled after native restart, disconnect, fixed
# log-file replacement, or developer transport mutation has started.
require_fixed 'platformProvider.restart(::beginCommit)' "$primary_service" \
    'platform restart commit boundary'
require_fixed 'runCommittedProviderRestart(beginCommit)' "$platform_provider_controller" \
    'native provider restart commit helper'
for restart_regression in \
    cancelledRestartDoesNotTouchNativeState committedRestartReturnsItsActualResult
do
    require_fixed "fun $restart_regression()" "$platform_provider_controller_test" \
        "provider restart boundary regression $restart_regression"
done
if ! awk '
    /override fun Disconnect\(\)/ { in_method = 1 }
    in_method && /if \(!beginCommit\(\)\)/ { commit = NR }
    in_method && /active\.disconnect\(\)/ { action = NR; in_method = 0 }
    END { exit !(commit && action && commit < action) }
' "$primary_service"; then
    fail "Watch1 Disconnect must commit before requesting disconnection"
fi
if ! awk '
    /override fun Dump\(\)/ { in_method = 1 }
    in_method && /if \(!beginCommit\(\)\)/ { commit = NR }
    in_method && /writeFixedLogFile/ { action = NR; in_method = 0 }
    END { exit !(commit && action && commit < action) }
' "$primary_service"; then
    fail "Logs1 Dump must commit before replacing the fixed log file"
fi
if ! awk '
    /override fun SetLocalEnabled\(enabled: Boolean\)/ { in_method = 1 }
    in_method && /if \(!beginCommit\(\)\)/ { commit = NR }
    in_method && /startDevConnection\(\).*stopDevConnection\(\)/ { action = NR; in_method = 0 }
    END { exit !(commit && action && commit < action) }
' "$primary_service"; then
    fail "Developer1 must commit before changing the local developer transport"
fi
require_fixed 'fun `cancelled scan mutation has no side effect and committed mutation runs once`()' \
    "$primary_discovery_mappings_test" 'scan mutation commit regression'
if ! awk '
    /override fun StartScan/ { in_method = 1 }
    in_method && /runCommittedScanMutation\(::beginCommit\)/ { commit = NR }
    in_method && /libPebble\.start(Ble|Classic)Scan\(\)/ { action = NR }
    in_method && /succeed\(/ { in_method = 0 }
    END { exit !(commit && action && commit <= action) }
' "$primary_service"; then
    fail "StartScan must commit immediately around its scan-state mutation"
fi
if ! awk '
    /override fun StopScan/ { in_method = 1 }
    in_method && /runCommittedScanMutation\(::beginCommit, ::stopAllScans\)/ { commit = NR }
    in_method && /succeed\(\)/ { success = NR; in_method = 0 }
    END { exit !(commit && success && commit < success) }
' "$primary_service"; then
    fail "StopScan must commit before stopping scan state"
fi

# Capability absence means unimplemented, not temporarily disconnected. Keep
# every implemented workflow visible; each method reports NotConnected or
# another runtime error when its temporary prerequisites are unavailable.
for capability in \
    watch.notifications watch.messaging watch.health \
    watch.screenshots watch.logs watch.developer-mode
do
    require_fixed "\"$capability\"" "$primary_service" \
        "implemented primary capability $capability"
done
require_fixed 'fun `implemented watch capabilities are independent of runtime connection state`()' \
    "$primary_watch_capabilities_test" 'stable primary watch capability regression'

# Pair and Watch1.Connect share one address-owned attempt registry. Pair is
# admitted before its path becomes visible; connection-goal publication races
# atomically with cancellation, and timed-out cleanup retains ownership until
# WatchManager confirms both the goal and transport attempt are gone.
require_fixed 'private val primaryConnectionAttempts = PrimaryConnectionAttemptRegistry()' \
    "$primary_service" 'shared primary connection-attempt registry'
require_fixed 'PrimaryConnectionAttemptKind.PAIR' "$primary_service" \
    'primary Pair attempt kind'
require_fixed 'PrimaryConnectionAttemptKind.WATCH_CONNECT' "$primary_service" \
    'primary Watch1 Connect attempt kind'
require_fixed 'primaryConnectionAttempts.claimAllCleanup(' "$primary_service" \
    'filtered CancelPairing connection cleanup'
require_fixed 'cancel = pairOperation::cancelFromManager' "$primary_service" \
    'committed CancelPairing authority over an active Pair operation'
require_fixed 'fun cancelFromManager()' "$primary_service" \
    'manager-owned Pair cancellation path'
require_fixed 'onPrepared = { pairOperation ->' "$primary_service" \
    'Pair admission before operation publication'
require_fixed 'onPrepared?.invoke(operation)' "$primary_service" \
    'operation prepared hook invocation'
require_fixed 'admittedAttempt?.let(primaryConnectionAttempts::abandonPending)' \
    "$primary_service" 'pre-start operation cancellation cleanup'
require_fixed 'requestConnectIfPending(attempt)' "$primary_service" \
    'atomic primary connection-goal publication'
require_fixed 'requestConnectionImmediately(device.identifier)' "$primary_service" \
    'ordered libpebble3 connection-goal seam'
require_fixed 'currentFailure != null && currentFailure != initialFailure' "$primary_service" \
    'new-attempt connection failure generation check'
reject_extended 'sawTransition' "$primary_service" \
    'identity-based connection-attempt completion check'
require_fixed 'if (primaryConnectionAttempts.complete(attempt))' \
    "$primary_service" 'primary Pair completion ownership boundary'
require_fixed 'cleanupPrimaryConnectionAttempts(listOf(attempt))' "$primary_service" \
    'primary unsuccessful connection-attempt cleanup'
require_fixed 'PrimaryConnectionCleanupState.RETIRING' "$primary_service" \
    'connection cleanup retains retirement ownership'
require_fixed 'primaryConnectionAttempts.retireConnectionIfCleaning(attempt)' \
    "$primary_service" 'ordered connection-goal retirement'
require_fixed 'cancelConnectionImmediately(identifier)' "$primary_service" \
    'concrete connection-goal cancellation'
require_fixed 'awaitConnectionRetired(identifier)' "$primary_service" \
    'authoritative connection retirement acknowledgement'
require_fixed 'scope.launch {' "$primary_service" \
    'post-timeout connection retirement reaper'
require_fixed 'concreteLibPebble::requestConnectionImmediately' "$daemon_main" \
    'ordered connection request wiring'
require_fixed 'concreteLibPebble::cancelConnectionImmediately' "$daemon_main" \
    'ordered connection cancellation wiring'
require_fixed 'concreteLibPebble::awaitConnectionRetired' "$daemon_main" \
    'connection retirement acknowledgement wiring'
for seam in requestConnectionImmediately cancelConnectionImmediately awaitConnectionRetired
do
    require_fixed "fun $seam" "$libpebble3_connection" \
        "concrete libpebble3 connection seam $seam"
done
require_fixed 'private val connectionGoalLock = SynchronizedObject()' "$libpebble3_watch_manager" \
    'WatchManager connection-goal ordering lock'
require_fixed 'import kotlinx.atomicfu.locks.synchronized' "$libpebble3_watch_manager" \
    'WatchManager multiplatform connection-goal lock API'
require_fixed 'if (device == null || !device.connectGoal)' "$libpebble3_watch_manager" \
    'stale collector connection-goal rejection'
require_fixed 'private fun detachConnection(identifier: PebbleIdentifier, connection: ConnectionScope)' \
    "$libpebble3_watch_manager" 'identity-safe WatchManager connection detachment'
require_fixed 'rollbackConnectionSetup(identifier, connectionKoinScope, connectionAttached)' \
    "$libpebble3_watch_manager" 'WatchManager connection-construction rollback'
require_fixed 'detachConnection(identifier, this@cleanup)' "$libpebble3_watch_manager" \
    'non-cancellable WatchManager cleanup retirement'
require_fixed 'disconnectFrom: disconnect request failed for $identifier' \
    "$libpebble3_watch_manager" 'WatchManager disconnect failure containment'
reject_extended 'device\.connect\(\)' "$primary_service" \
    'asynchronous device-level connection request on the primary API'
if ! awk '
    /override fun Pair\(/ { in_method = 1 }
    in_method && /runCommittedScanMutation\(::beginCommit, ::stopAllScans\)/ { commit = NR }
    in_method && /requestConnectIfPending\(attempt\)/ { connect = NR; in_method = 0 }
    END { exit !(commit && connect && commit < connect) }
' "$primary_service"; then
    fail "Pair must commit before publishing its ordered connection goal"
fi
if ! awk '
    /private fun operation\(/ { in_method = 1 }
    in_method && /onPrepared\?\.invoke\(operation\)/ { prepared = NR }
    in_method && /managedObjects\.mutate/ { published = NR; in_method = 0 }
    END { exit !(prepared && published && prepared < published) }
' "$primary_service"; then
    fail "operation preparation hooks must run before ObjectManager publication"
fi
if ! awk '
    /override fun CancelPairing\(/ { in_method = 1 }
    in_method && /runCommittedScanMutation\(::beginCommit, ::stopAllScans\)/ { commit = NR }
    in_method && /primaryConnectionAttempts\.claimAllCleanup/ { cancel = NR; in_method = 0 }
    END { exit !(commit && cancel && commit < cancel) }
' "$primary_service"; then
    fail "CancelPairing must commit before changing scan or pairing state"
fi
for connection_regression in \
    'successful pair cannot be claimed by later cancellation' \
    'pair and watch connect are mutually exclusive case insensitively' \
    'abandoned pending attempt cannot request a connection' \
    'cancel pairing wins before queued pair connect request' \
    'started connection is cancelled and retained while retiring' \
    'manager cancellation still cancels a pair after pair commit' \
    'pair and watch connect share the request and retirement lifecycle' \
    'cleanup timeout stays busy until retirement is acknowledged' \
    'replacement connection waits until stale cleanup releases the address' \
    'cancel all claims and cancels every pending operation' \
    'cancel all joins cleanup already claimed by operation cancellation' \
    'retirement waiter is single owner until acknowledgement' \
    'retirement acknowledgement cannot overtake goal cancellation' \
    'cancel pairing filters watch connect attempts'
do
    require_fixed "fun \`$connection_regression\`()" "$primary_connection_attempt_test" \
        "primary connection-attempt regression $connection_regression"
done
for completion_regression in \
    'stale initial failure is not terminal for a retry' \
    'incremented failure is terminal for a retry' \
    'different failure is terminal for a retry' \
    'connected is terminal despite a stale failure'
do
    require_fixed "fun \`$completion_regression\`()" "$connection_attempt_completion_test" \
        "primary connection-completion regression $completion_regression"
done
for watch_manager_regression in \
    immediateConnectionRequestThenCancellationDoesNotStartConnector \
    cancelledImmediateConnectionDoesNotResumeWhenBluetoothIsEnabled \
    awaitConnectionRetiredPreventsCancelledAttemptFromReappearing \
    concurrentStateFlowUpdateDoesNotCreateDuplicateConnectionScopes \
    disconnectFailureStillRetiresConnectionBeforeRetry \
    scopeCloseFailureStillRetiresConnectionBeforeRetry \
    scopeFactoryFailureRetiresGoalAndAllowsExplicitRetry
do
    require_fixed "fun $watch_manager_regression()" "$libpebble3_watch_manager_test" \
        "ordered WatchManager connection-goal regression $watch_manager_regression"
done

# Bond import is a stable Discovery1 workflow: its public method must reach
# the injected concrete importer, advertise its capability, and never regress
# to the prior placeholder NotSupported operation.
require_fixed 'override fun ImportBondedWatches(): DBusPath = operation("discovery.import-bonds") {' \
    "$primary_service" 'bond import D-Bus operation'
require_fixed 'runBondedWatchImport(importBondedWatches, ::beginCommit)' "$primary_service" \
    'bond import importer wiring'
require_fixed 'add("discovery.bond-import")' "$primary_discovery_mappings" \
    'bond import capability advertisement'
require_fixed 'BondedWatchImportOutcome.PersistenceFailed' "$bond_import_operation" \
    'bond import persistence-failure mapping'
reject_extended 'bond import awaits upstream libpebble3 support' "$primary_service" \
    'obsolete bond import NotSupported placeholder'

# Forget is one logical-watch transaction across the compatibility and primary
# APIs.  Resolve every selected-adapter transport alias before retiring the
# portable record, and let Connect supersede the operation until commit begins.
require_fixed 'val bondedWatchForget = createBluezBondedWatchForgetCoordinator()' \
    "$daemon_main" 'one shared bonded-watch Forget coordinator'
require_fixed 'BluezManager.prepareBondRemoval(address, name)' \
    "$bond_forget_coordinator" 'selected-adapter alias-removal snapshot'
require_fixed 'connectionsWhilePreparing' "$bond_forget_coordinator" \
    'Connect race protection during alias discovery'
require_fixed 'planJvmBondedWatchRemoval(devices, name, address)' \
    "$linux_bluez_manager" 'BlueZ alias-removal planning'
require_fixed 'classifyJvmBondedPebble(' "$linux_bonded_watch_seeder" \
    'Pebble transport evidence for removal aliases'
for service in "$primary_service" "$compat_service"
do
    require_fixed 'bondedWatchForget.forget(' "$service" \
        'shared alias-aware Forget workflow'
    require_fixed 'bondedWatchForget.connect(address)' "$service" \
        'Connect supersession of pending Forget'
done
if ! awk '
    index($0, "if (!beginCommit())") { commit = NR }
    index($0, "withContext(NonCancellable)") { noncancellable = NR }
    index($0, "disconnect()") { disconnect = NR }
    index($0, "if (!prepared.remove())") { remove = NR }
    index($0, "forgetPortable()") { portable = NR }
    END {
        exit !(commit && noncancellable && disconnect && remove && portable &&
            commit < noncancellable && noncancellable < disconnect &&
            disconnect < remove && remove < portable)
    }
' "$bond_forget_coordinator"
then
    fail "Forget must commit before non-cancellable disconnect, bond removal and retirement"
fi
for forget_regression in \
    'removes every alias before retiring portable state' \
    'reconnect through either alias is rejected after forget commits' \
    'unavailable cancelled and failed cleanup preserve portable state'
do
    require_fixed "fun \`$forget_regression\`()" "$bond_forget_coordinator_test" \
        "bonded-watch Forget regression $forget_regression"
done

# A declared watch interface may fail with Operation1.NotSupported, but must
# never disappear as UnknownMethod while a client is probing capability.
for watch_interface in \
    RockpoolFirmware1 RockpoolApplications1 RockpoolTimeline1 \
    RockpoolNotifications1 RockpoolMessaging1 RockpoolHealth1 \
    RockpoolProfiles1 RockpoolScreenshots1 RockpoolLogs1 RockpoolDeveloper1
do
    require_fixed "$watch_interface" "$primary_service" \
        "implemented watch-domain interface $watch_interface"
done

# Loader discovery must remain package-bound and fail closed before dlopen.
for loader_guard in 'O_NOFOLLOW' 'openat(' 'RTLD_LOCAL' 'request_stop' 'get_status'
do
    require_fixed "$loader_guard" "$loader" "provider-loader guard $loader_guard"
done
require_fixed '/work/tests/platform_loader_event_test.c' "$native_build" \
    'provider event/command native unit test in packaged build'
require_fixed 'static int begin_provider_command_dispatch(uint64_t command_domain)' \
    "$loader" 'provider command generation gate'
require_fixed 'provider_reset_pending ||' "$loader" \
    'provider command reset-pending rejection'
require_fixed '(event_failed_domains & command_domain) != 0' "$loader" \
    'provider command failed-domain rejection'
require_fixed '++provider_command_dispatches' "$loader" \
    'provider command generation reservation'
require_fixed 'while (provider_command_dispatches != 0)' "$loader" \
    'provider reset waits for admitted commands'
require_fixed 'pthread_cond_wait(&provider_command_condition, &event_lock)' \
    "$loader" 'provider reset generation handoff'
for command_domain in NOTIFICATIONS CALLS MEDIA
do
    require_fixed "LP3_PLATFORM_DOMAIN_$command_domain);" "$loader" \
        "provider command generation gate for $command_domain"
done
require_fixed 'assert(provider_command_dispatches == 1)' \
    "$platform_loader_event_test" \
    'provider command reserves current generation through dispatch regression'
require_fixed 'enqueue_media(55)' "$platform_loader_event_test" \
    'command-triggered event remains deliverable before completion regression'
require_fixed 'pthread_create(&reset_thread, NULL, enqueue_reset_thread, NULL)' \
    "$platform_loader_event_test" \
    'provider reset cannot cross in-flight command regression'
require_fixed 'event_failed_domains = LP3_PLATFORM_DOMAIN_CALLS' \
    "$platform_loader_event_test" \
    'domain-specific provider command rejection regression'

# Classic uses the legacy backend's direct outbound RFCOMM channel-1 stream. The
# packaged loader owns the JNI bridge, while libpebble3 owns protocol pumping,
# partial writes and connection lifecycle. Do not regress to an unavailable
# generic socket or silently omit the bridge from the Native Image package.
for rfcomm_guard in 'AF_BLUETOOTH' 'BTPROTO_RFCOMM' 'MSG_NOSIGNAL' 'rfcomm_shutdown'
do
    require_fixed "$rfcomm_guard" "$rfcomm_socket" "RFCOMM transport guard $rfcomm_guard"
done
require_fixed '/work/native/rfcomm_socket.c' "$native_build" \
    'RFCOMM bridge in packaged native loader'
require_fixed '/work/tests/rfcomm_socket_test.c' "$native_build" \
    'RFCOMM native unit test in packaged build'
for rfcomm_jni_method in \
    rfcommCreate rfcommConnect rfcommRead rfcommWrite rfcommShutdown rfcommDestroy
do
    require_fixed "\"name\": \"$rfcomm_jni_method\"" "$jni_config" \
        "Native Image RFCOMM JNI method $rfcomm_jni_method"
done
require_fixed 'LEGACY_RFCOMM_CHANNEL = 1' "$linux_classic_connector" \
    'legacy Pebble RFCOMM channel'
require_fixed 'bind LinuxRfcommSocketFactory::class' "$platform_provider_module" \
    'Sailfish RFCOMM factory override'
require_fixed 'supportsBtClassic = rfcommSocketFactory.available' "$platform_provider_module" \
    'runtime-gated Classic platform configuration'

# The loader accepts an exact regular .so and deliberately rejects symlinks.
# qmake must therefore build the provider as a plugin (not a versioned shared
# library with an unversioned symlink).  Rockpool owns the ABI development
# package and compiles its provider against that package's canonical in-tree
# header, avoiding a self-BuildRequire.
require_fixed 'unversioned_libname' "$proxy_project" \
    'unversioned proxy plugin configuration'
require_fixed '%global lp3_platform_sdk_version 1.4' "$rockpool_spec" \
    'fixed platform ABI SDK version'
require_fixed 'BuildArch:  noarch' "$rockpool_spec" \
    'architecture-neutral platform ABI development package'
require_fixed '%{_datadir}/pkgconfig/libpebble3d-platform.pc' "$rockpool_spec" \
    'architecture-neutral platform ABI pkg-config module'
for provider_project in "$launcher_project" "$proxy_project" "$helper_project"
do
    require_fixed '../../libpebble3d/include' "$provider_project" \
        'canonical in-tree platform ABI include path'
    reject_extended 'PKGCONFIG.*libpebble3d-platform' "$provider_project" \
        'self-referential platform ABI pkg-config input'
done
reject_extended '^BuildRequires:[[:space:]]+pkgconfig\(libpebble3d-platform\)' \
    "$rockpool_spec" 'self-referential platform ABI BuildRequires'
reject_extended '^%package -n libpebble3d-platform-devel' "$package_spec" \
    'platform development package in Native Image spec'
require_fixed 'Provides:   rockwork-dbus-compat = 1' "$package_spec" \
    'temporary Rockwork compatibility virtual provide'
require_fixed 'Provides:   libpebble3d-platform-launcher-abi = 1' "$package_spec" \
    'private session-launcher protocol capability'
require_fixed '#define LP3_PLATFORM_ABI_MINOR 4u' "$header" \
    'public platform ABI minor 1.4'
require_fixed '"1.4"' "$loader" \
    'native platform snapshot ABI version 1.4'
require_fixed 'val abiVersion: String = "1.4"' "$platform_provider_controller" \
    'daemon platform snapshot ABI default 1.4'
require_fixed 'field(3).ifEmpty { "1.4" }' "$platform_provider_controller" \
    'daemon platform snapshot ABI fallback 1.4'
require_fixed 'Provides:   libpebble3d-platform-abi-minor = 4' "$package_spec" \
    'runtime platform ABI minor capability'
require_fixed '%attr(0755,root,root) /usr/libexec/libpebble3d/*.so' "$package_spec" \
    'package-owned native-image support libraries'
require_fixed 'Requires:   libpebble3d-platform-launcher-abi = 1' "$rockpool_spec" \
    'provider dependency on daemon launcher bootstrap'
require_fixed 'Requires:   libpebble3d-platform-abi-minor >= 4' "$rockpool_spec" \
    'provider dependency on runtime platform ABI minor'
require_fixed 'static const uint16_t kMinor = 4;' "$wire_header" \
    'private provider wire minor 1.4'
require_fixed 'MessageReply = 5,' "$wire_header" \
    'private typed message-reply operation'
require_fixed 'NotificationHasReplyAction = 1u << 1,' "$wire_header" \
    'notification reply-capability flag'
require_fixed 'DomainMessaging = 1u << 1,' "$wire_header" \
    'private messaging domain'
require_fixed 'DomainNotifications | DomainMessaging |' "$wire_header" \
    'messaging domain included in complete health validation'
require_fixed 'kMessageTextMax = 512' "$wire_header" \
    'bounded message-reply text'
require_fixed 'validNotificationId(reply.notificationId)' "$wire_header" \
    'numeric notification ID message-reply bound'
require_fixed 'validMessageReply' "$wire_header" \
    'typed message-reply codec validation'

# Only the tiny session launcher may acquire the privileged group.  It must be
# launched by the real user manager, withhold the host until the daemon is
# non-dumpable, and execute only fixed package-owned paths.  The proxy consumes
# an inherited control descriptor and must never spawn a setgid process.
require_fixed 'validate_user_manager_parent' "$launcher_source" \
    'user-manager parent authorization'
require_fixed 'setegid(session.gid)' "$launcher_source" \
    'session-fscred parent validation'
require_fixed 'setegid(session.privileged_gid)' "$launcher_source" \
    'saved setgid credential restoration'
require_fixed 'validate_service_cgroup' "$launcher_source" \
    'canonical user-service cgroup authorization'
require_fixed 'PR_SET_PDEATHSIG' "$launcher_source" \
    'launcher and child parent-death handling'
require_fixed 'setresgid(session->gid' "$launcher_source" \
    'irreversible daemon group drop'
require_fixed 'setresgid(session->privileged_gid' "$launcher_source" \
    'fixed host privileged group normalization'
require_fixed 'unexpected_control' "$launcher_source" \
    'unexpected launcher-control descriptor rejection'
require_fixed 'LP3_LAUNCHER_DAEMON_READY' "$launcher_source" \
    'non-dumpable daemon readiness gate'
require_fixed 'PR_SET_DUMPABLE' "$loader" \
    'daemon procfs hardening before launcher acknowledgement'
require_fixed 'F_DUPFD_CLOEXEC' "$proxy_source" \
    'inherited launcher control descriptor use'
reject_extended 'posix_spawn|fork\(' "$proxy_source" \
    'provider-side process spawning'
require_fixed 'setresgid(privileged->gr_gid' "$helper_source" \
    'inherited privileged group normalization before Qt'
require_fixed 'MDConfItem' "$helper_source" \
    'typed Sailfish dconf access'
require_fixed '/sailfish/i18n/lc_timeformat24h' "$helper_source" \
    'fixed Sailfish time-format key'
require_fixed 'decodeTimeGet' "$helper_source" \
    'typed helper time dispatcher'
require_fixed 'LP3_PLATFORM_DOMAIN_TIME' "$proxy_source" \
    'advertised typed time domain'
require_fixed 'LP3_PLATFORM_DOMAIN_NOTIFICATIONS' "$proxy_source" \
    'advertised typed notification domain'
require_fixed 'LP3_PLATFORM_DOMAIN_MESSAGING' "$proxy_source" \
    'advertised typed messaging domain'
require_fixed 'LP3_PLATFORM_DOMAIN_CALLS' "$proxy_source" \
    'advertised typed calls domain'
require_fixed 'LP3_PLATFORM_DOMAIN_MEDIA' "$proxy_source" \
    'advertised typed system-volume domain'
require_fixed 'drainNotificationEvents' "$loader" \
    'bounded JNI notification event drain'
require_fixed 'notificationCommand' "$loader" \
    'typed JNI notification action'
for calls_jni_method in beginEventBatch endEventBatch drainCallEvents callCommand
do
    require_fixed "\"name\": \"$calls_jni_method\"" "$jni_config" \
        "Native Image Calls JNI method $calls_jni_method"
done
for media_jni_method in drainMediaEvents mediaCommand
do
    require_fixed "\"name\": \"$media_jni_method\"" "$jni_config" \
        "Native Image Media JNI method $media_jni_method"
done
require_fixed 'int32_t (*reply_message)' "$header" \
    'public typed notification reply entry point'
require_fixed '"name": "replyMessage"' "$jni_config" \
    'Native Image message-reply JNI method'
require_fixed 'Java_io_rebble_libpebblecommon_rockpool_PlatformProviderNative_replyMessage' \
    "$loader" 'bounded native message-reply JNI bridge'
require_fixed 'LP3_PLATFORM_MESSAGE_TEXT_MAX' "$loader" \
    'native message-reply text bound'
require_fixed 'const val MESSAGING_DOMAIN = 1L shl 1' "$platform_provider_controller" \
    'daemon messaging-domain capability'
require_fixed 'val requiredDomains = NOTIFICATION_DOMAIN or MESSAGING_DOMAIN' \
    "$platform_provider_controller" 'dual-domain message-reply gate'
require_fixed 'PlatformProviderNative.replyMessage(id, text)' \
    "$platform_provider_controller" 'typed daemon message-reply dispatch'

# Calls and volume commands originate on libpebble3's watch-processing path.
# Provider I/O must run asynchronously through bounded FIFO workers. Call
# actions reserve the current generation before enqueue so duplicates cannot
# crowd out a later action; volume work carries a media generation so queued
# commands cannot survive provider loss or replacement.
require_fixed 'class PlatformCallsBackend' "$platform_calls_backend" \
    'Sailfish provider calls adapter'
require_fixed 'commandAllowedLocked(command, id, generation)' "$platform_calls_backend" \
    'provider call command enqueue authorization'
require_fixed 'private fun commandAllowedLocked(' "$platform_calls_backend" \
    'shared provider call command authorization implementation'
require_fixed 'Channel<PendingCommand>(MAX_PENDING_COMMANDS)' "$platform_calls_backend" \
    'bounded provider call command queue'
require_fixed 'for (pending in commands)' "$platform_calls_backend" \
    'serialized provider call command consumer'
require_fixed 'reserveCommandLocked' "$platform_calls_backend" \
    'provider call reservation before bounded enqueue'
require_fixed 'commandReservationCurrentLocked(pending)' "$platform_calls_backend" \
    'current-generation provider call reservation gate'
require_fixed 'private const val MAX_PENDING_COMMANDS = 8' "$platform_calls_backend" \
    'bounded provider call command capacity'
if ! awk '
    /suspend fun callCommand\(/ { in_method = 1 }
    in_method && /lifecycleLock\.withLock/ && !locked { locked = NR }
    in_method && /if \(!isCurrent\(\)\)/ && !gated { gated = NR }
    in_method && /PlatformProviderNative\.callCommand/ { native = NR; exit }
    END { exit !(locked && locked < gated && gated < native) }
' "$platform_provider_controller"; then
    fail "provider call generation is not checked inside the lifecycle lock before native dispatch in $platform_provider_controller"
fi
for calls_regression in \
    queuedAnswerIsDroppedWhenCallEndsBeforeDispatcherRelease \
    queuedHangupIsDroppedWhenReplacementCallArrivesBeforeDispatcherRelease \
    queuedCommandIsDroppedWhenProviderResetsBeforeDispatcherRelease \
    queuedCurrentCallCommandExecutesWhenDispatcherReleases
do
    require_fixed "fun $calls_regression()" "$platform_calls_backend_test" \
        "queued provider call regression $calls_regression"
done
for calls_regression in \
    'answer then hangup executes in FIFO order' \
    'hangup suppresses later answer before its command finishes' \
    'duplicate answer and hangup callbacks are suppressed' \
    'duplicate queued answers cannot crowd out hangup' \
    'provider reset after dequeue rejects stale call command' \
    'failed hangup releases its claim for a later answer'
do
    require_fixed "fun \`$calls_regression\`" "$platform_calls_backend_test" \
        "serialized provider call regression $calls_regression"
done
require_fixed 'class PlatformVolumeControl' "$platform_volume_control" \
    'Sailfish provider system-volume adapter'
require_fixed ': VolumeControl' "$platform_volume_control" \
    'native-Linux system-volume implementation'
require_fixed 'Channel<PendingCommand>(MAX_PENDING_COMMANDS)' "$platform_volume_control" \
    'bounded ordered provider volume command queue'
require_fixed 'commandScope.launch {' "$platform_volume_control" \
    'asynchronous provider volume command consumer'
require_fixed 'commands.trySend(pending)' "$platform_volume_control" \
    'nonblocking provider volume command submission'
require_fixed 'pending.epoch == mediaEpoch' "$platform_volume_control" \
    'stale provider volume command rejection'
require_fixed 'private const val MAX_PENDING_COMMANDS = 32' "$platform_volume_control" \
    'bounded provider volume command capacity'
if ! awk '
    /suspend fun mediaCommand\(/ { in_method = 1 }
    in_method && /lifecycleLock\.withLock/ && !locked { locked = NR }
    in_method && /if \(!isCurrent\(\)\)/ && !gated { gated = NR }
    in_method && /PlatformProviderNative\.mediaCommand/ { native = NR; exit }
    END { exit !(locked && locked < gated && gated < native) }
' "$platform_provider_controller"; then
    fail "provider media generation is not checked inside the lifecycle lock before native dispatch in $platform_provider_controller"
fi
reject_extended 'runBlocking' "$platform_volume_control" \
    'blocking provider volume dispatch'
for volume_regression in \
    'step returns while provider command is held' \
    'rapid signed steps preserve provider command order' \
    'media-domain loss drops commands queued behind an in-flight step' \
    'media-domain loss after dequeue rejects command before native dispatch' \
    'steps beyond the bounded queue are dropped while a command is held' \
    'failed provider command does not prevent later commands'
do
    require_fixed "fun \`$volume_regression\`()" "$platform_volume_control_test" \
        "provider volume dispatch regression $volume_regression"
done
require_fixed 'bind VolumeControl::class' "$platform_provider_module" \
    'provider system-volume override'
require_fixed 'MprisController' "$linux_music_control" \
    'generic MPRIS metadata and transport controls'
require_fixed 'com.Meego.MainVolume2' "$main_volume_monitor" \
    'fixed Sailfish system-volume service interface'
require_fixed 'kMaximumStepCount' "$main_volume_monitor" \
    'bounded Sailfish system-volume step range'
require_fixed 'LP3_PLATFORM_MEDIA_VOLUME_UP' "$main_volume_monitor" \
    'typed system-volume increase command'
require_fixed 'LP3_PLATFORM_MEDIA_VOLUME_DOWN' "$main_volume_monitor" \
    'typed system-volume decrease command'

# These regressions must be compiled and executed by the RPM source build;
# merely leaving test sources in a developer worktree is not a release gate.
require_fixed '%check' "$rockpool_spec" 'Rockpool package test phase'
for helper_test_project in \
    callmonitor_test.pro mainvolumemonitor_test.pro notificationmonitor_test.pro \
    stop_handshake_test.pro wire_test.pro
do
    require_fixed "../platform-sailfish/tests/$helper_test_project" "$rockpool_spec" \
        "packaged Sailfish helper regression project $helper_test_project"
done
for helper_test_binary in \
    callmonitor_test mainvolumemonitor_test notificationmonitor_test \
    stop_handshake_test wire_test
do
    require_fixed "./$helper_test_binary" "$rockpool_spec" \
        "executed Sailfish helper regression $helper_test_binary"
done
require_fixed 'SOURCES += callmonitor_test.cpp' "$call_monitor_test_project" \
    'calls-monitor regression source wiring'
require_fixed '../helper/callmonitor.cpp' "$call_monitor_test_project" \
    'calls-monitor implementation under test'
require_fixed 'SOURCES += mainvolumemonitor_test.cpp' "$main_volume_monitor_test_project" \
    'system-volume regression source wiring'
require_fixed '../helper/mainvolumemonitor.cpp' "$main_volume_monitor_test_project" \
    'system-volume implementation under test'
require_fixed 'DEFINES += LP3_MAINVOLUME_TEST' "$main_volume_monitor_test_project" \
    'test-only MainVolume2 peer-address seam'
require_fixed 'ROCKPOOL_TEST_PULSE_SOCKET' "$main_volume_monitor_test" \
    'buildroot-safe private PulseAudio socket fixture path'
require_fixed '#ifdef LP3_MAINVOLUME_TEST' "$main_volume_monitor" \
    'production-safe scope for MainVolume2 test socket override'
require_fixed '++ownerGeneration;' "$main_volume_monitor" \
    'MainVolume2 owner-change snapshot invalidation'
require_fixed 'servicePresenceSnapshotCurrent(generation, owner)' \
    "$main_volume_monitor" 'MainVolume2 stale NameHasOwner reply rejection'
require_fixed 'applyServicePresenceSnapshotForTest' "$main_volume_monitor_test" \
    'stale MainVolume2 NameHasOwner snapshot regression'
require_fixed 'setRemoteCurrentStep' "$main_volume_monitor_test" \
    'independent MainVolume2 StepsUpdated regression'
require_fixed 'lookupRepliesHandledForTest() > lookupReplies' \
    "$main_volume_monitor_test" \
    'processed stale MainVolume2 lookup-reply regression'
require_fixed 'sessionGenerationForTest() == presenceSession' \
    "$main_volume_monitor_test" \
    'MainVolume2 owner-only snapshot invalidation regression'
require_fixed 'assert(!monitor.peerActiveForTest())' "$main_volume_monitor_test" \
    'stale MainVolume2 lookup cannot activate a peer regression'
require_fixed 'SOURCES += stop_handshake_test.cpp' "$stop_handshake_test_project" \
    'STOP_HOST regression source wiring'
require_fixed 'SOURCES += notificationmonitor_test.cpp' "$notification_monitor_test_project" \
    'notification-reply regression source wiring'
require_fixed '#include "../helper/notificationmonitor.cpp"' "$notification_monitor_test" \
    'notification-reply implementation under test'
require_fixed 'SOURCES += wire_test.cpp' "$wire_test_project" \
    'provider wire regression source wiring'
require_fixed '../common/wire.h' "$wire_test_project" \
    'provider wire implementation under test'
for stop_regression in \
    testStopReplyIsConsumedWhileStopping \
    testRegularReplyWaitRemainsInterruptible \
    testExpectedStartSkipsLateStop \
    testExpectedStopClosesLateStartedDescriptor \
    testHealthReadyLossPublishesAuthorityBarrier
do
    require_fixed "void $stop_regression()" "$stop_handshake_test" \
        "launcher STOP_HOST regression $stop_regression"
done
require_fixed 'lostDomains = instance->readyDomains & ~health.readyDomains &' \
    "$proxy_source" 'provider ready-domain loss authority barrier'
require_fixed 'publishDomainLoss(lostDomains, callback, context);' \
    "$proxy_source" 'provider domain-loss event publication'
require_fixed 'interface LinuxNotificationBackend' "$linux_notification_backend" \
    'injectable native-Linux notification backend'
require_fixed 'class FreedesktopNotificationBackend' "$linux_notification_backend" \
    'generic freedesktop notification backend'
require_fixed 'class PlatformNotificationBackend' "$platform_notification_backend" \
    'Sailfish provider notification adapter'
require_fixed ': LinuxNotificationBackend' "$platform_notification_backend" \
    'native-Linux notification backend implementation'
require_fixed 'fun providerGenerationBoundaryResetsNotificationsBeforeDomainDrains()' \
    "$platform_provider_controller_test" \
    'provider-generation notification reset regression'
require_fixed 'private var actionEpoch = 0L' "$platform_notification_backend" \
    'provider notification action generation'
require_fixed 'if (!notificationsReady.get() || resetQueued ||' \
    "$platform_notification_backend" \
    'provider notification/reset action readiness gate'
require_fixed 'command is LinuxNotificationCommand.Reply && !messagingReady.get()' \
    "$platform_notification_backend" \
    'provider message-reply messaging-domain readiness gate'
require_fixed 'command !is LinuxNotificationCommand.Reply || messagingReady.get()' \
    "$platform_notification_backend" \
    'provider message-reply current messaging-domain gate'
require_fixed 'actionEpoch++' "$platform_notification_backend" \
    'provider notification reset invalidates action authority'
if ! awk '
    /suspend fun notificationCommand\(/ { in_method = 1 }
    in_method && /lifecycleLock\.withLock/ && !locked { locked = NR }
    in_method && /if \(!isCurrent\(\)\)/ && !gated { gated = NR }
    in_method && /current\.domains and NOTIFICATION_DOMAIN/ && !domain { domain = NR }
    in_method && /PlatformProviderNative\.notificationCommand/ { native = NR; exit }
    END { exit !(locked && locked < gated && gated < domain && domain < native) }
' "$platform_provider_controller"; then
    fail "provider notification generation is not checked inside the lifecycle lock before native dispatch in $platform_provider_controller"
fi
for notification_action_regression in \
    dismissDequeuedBeforeResetBarrierIsNotForwarded \
    dismissDequeuedBeforeDomainLossIsNotForwardedToReplacement
do
    require_fixed "fun $notification_action_regression()" \
        "$platform_provider_controller_test" \
        "provider notification action regression $notification_action_regression"
done
if ! awk '
    /val fieldEvents =/ { fields = NR }
    /applyProviderGenerationBoundary\(fieldEvents\)/ { boundary = NR }
    /PlatformProviderNative\.drainNotificationEvents\(\)/ { notifications = NR }
    END { exit !(fields && boundary && notifications && fields < boundary && boundary < notifications) }
' "$platform_provider_controller"; then
    fail "provider generation boundary must precede fallible notification draining"
fi
require_fixed 'bind LinuxNotificationBackend::class' "$platform_provider_module" \
    'provider notification backend override'
reject_extended 'rockpool|PlatformProviderController|LP3_PLATFORM' \
    "$linux_notification_backend" \
    'Rockpool or provider dependency in native-Linux notification backend'
reject_extended 'NotificationAppRealDao|buildTimelineNotification|appearanceFor|MuteState' \
    "$platform_notification_backend" \
    'notification policy duplicated in provider transport adapter'
require_fixed 'buildTimelineNotification' "$linux_notification_listener" \
    'native-Linux notification pin construction'
require_fixed 'fun interface LinuxPairingRequester' "$linux_pairing" \
    'injectable native-Linux pairing requester'
require_fixed 'fun interface LinuxBluezAdapterSelector' "$linux_bluez_manager" \
    'injectable native-Linux BlueZ adapter selector'
require_fixed 'class SailfishPairingRequester' "$sailfish_linux_backend" \
    'Sailfish pairing requester implementation'
require_fixed 'class SailfishBluezAdapterSelector' "$sailfish_linux_backend" \
    'Sailfish BlueZ adapter-selection implementation'
reject_tree_extended \
    'rockpool|sailfish|libpebble3d|org\.rockwork|org\.rockpool|com\.jolla|x-nemo|alienbt|appsupport|lipstick|mkcal|qtcontacts|nemo' \
    'product-specific identity or policy in libpebble3 source' \
    "$libpebble3_jvm_source" "$libpebble3_common_source"
require_fixed 'eavesdrop=' "$notification_monitor" \
    'private notification monitor match rule'
require_fixed 'CloseNotification' "$notification_monitor" \
    'fixed notification dismissal target'
require_fixed 'activeIds.contains' "$notification_monitor" \
    'notification dismissal active-ID check'
require_fixed 'dbus_connection_send_with_reply_and_block' "$notification_monitor" \
    'bounded notification dismissal reply'
require_fixed 'self->serviceOwner' "$notification_monitor" \
    'unique notification-service sender validation'
require_fixed "type='error',eavesdrop='true'" "$notification_monitor" \
    'failed notification reply correlation cleanup'
require_fixed 'kPendingTimeoutMs' "$notification_monitor" \
    'bounded notification reply correlation lifetime'
require_fixed 'if (removeActive(id))' "$notification_monitor" \
    'tracked-only notification close delivery'
require_fixed 'const int kMaximumActive = 32' "$notification_monitor" \
    'bounded native notification authority'
reject_extended "method_return',sender='org\\.freedesktop\\.Notifications" \
    "$notification_monitor" 'Sailfish-incompatible well-known reply sender match'
require_fixed 'getNameOwner(connection, kCommHistoryService' "$notification_monitor" \
    'current CommHistory unique-owner lookup'
require_fixed 'pending.sender != commHistoryOwner' "$notification_monitor" \
    'trusted CommHistory notification sender gate'
require_fixed 'commHistoryOwnerChanged' "$notification_monitor" \
    'CommHistory owner-change reply-authority revocation'
require_fixed 'replyTargets.clear();' "$notification_monitor" \
    'reply authority cleared on owner change'
for reply_category in x-nemo.messaging.sms x-nemo.messaging.im x-nemo.messaging.mms
do
    require_fixed "$reply_category" "$notification_monitor" \
        "trusted message-reply category $reply_category"
done
require_fixed 'pending.hints.value(typeName).toString() != QStringLiteral("input")' \
    "$notification_monitor" 'input-only reply action gate'
require_fixed 'parts.size() != 6' "$notification_monitor" \
    'exact two-route-argument reply hint shape'
require_fixed 'const char kMessagesService[] = "org.sailfishos.Messages";' \
    "$notification_monitor" 'fixed Sailfish Messages reply service'
require_fixed 'const char kMessagesPath[] = "/";' "$notification_monitor" \
    'fixed Sailfish Messages reply path'
require_fixed 'const char kMessagesInterface[] = "org.sailfishos.Messages";' \
    "$notification_monitor" 'fixed Sailfish Messages reply interface'
require_fixed 'const char kMessagesMethod[] = "sendMessage";' \
    "$notification_monitor" 'fixed Sailfish Messages reply member'
require_fixed 'kMessagesService, kMessagesPath, kMessagesInterface,' \
    "$notification_monitor" 'fixed typed Sailfish Messages reply destination'
require_fixed 'kMessagesMethod);' "$notification_monitor" \
    'fixed typed Sailfish Messages reply member use'
require_fixed 'DBUS_TYPE_STRING, &accountValue' "$notification_monitor" \
    'first QString reply route argument'
require_fixed 'DBUS_TYPE_STRING, &recipientValue' "$notification_monitor" \
    'second QString reply route argument'
require_fixed 'DBUS_TYPE_STRING, &textValue' "$notification_monitor" \
    'bounded reply text argument'
require_fixed 'replyTargets.remove(numericId);' "$notification_monitor" \
    'one-shot helper reply authority consumption'
require_fixed 'LP3_PLATFORM_NOTIFICATION_HAS_REPLY_ACTION' "$notification_monitor" \
    'notification reply-capability publication'
require_fixed 'LP3_PLATFORM_MESSAGE_TEXT_MAX' "$notification_monitor" \
    'helper reply-text bound'
reject_extended 'ChannelDispatcher|\.DRAFT|setArguments' "$notification_monitor" \
    'legacy or arbitrary notification route execution'
for reply_test_contract in testExactReplyCapability testRejectsInvalidCapability \
    testRejectsNonCanonicalSerializedArguments
do
    require_fixed "void $reply_test_contract()" "$notification_monitor_test" \
        "notification-reply regression $reply_test_contract"
done
require_fixed 'org.sailfishos.Messages' "$notification_monitor_test" \
    'fixed-route notification-reply regression'
require_fixed 'x-nemo.messaging.mms' "$notification_monitor_test" \
    'MMS notification-reply regression'
require_fixed 'assert(!replyTarget(' "$notification_monitor_test" \
    'rejected notification-reply authority regression'
require_fixed 'PKGCONFIG += dbus-1 mlite5' "$helper_project" \
    'notification and MDConfItem build dependencies'
require_fixed 'packageOwned(path, 0750' "$helper_source" \
    'non-setgid host mode validation'
require_fixed 'ExecStart=/usr/libexec/libpebble3d/libpebble3d-platform-sailfish-launcher' \
    "$service_dropin" 'session launcher service override'
require_fixed '%attr(2755,root,privileged) %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-launcher' \
    "$rockpool_spec" 'setgid launcher package mode'
require_fixed '%attr(0750,root,privileged) %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-host' \
    "$rockpool_spec" 'non-setgid host package mode'

# The Native Image must not retain its old setgid-era Kotlin Sailfish module:
# platform access is now isolated in the proxy/helper package.  Keeping that
# module would reintroduce direct privileged SQLite and D-Bus access.
if [ -d "$sailfish_module_dir" ] && \
    find "$sailfish_module_dir" -type f -print -quit | grep -q .
then
    fail "obsolete direct Sailfish module remains at $sailfish_module_dir"
fi
reject_extended 'libpebblecommon\.sailfish' "$daemon_main" \
    'direct Sailfish module reference'
reject_extended 'libpebblecommon\.sailfish' "$reflect_config" \
    'stale direct Sailfish reflection entry'
reject_extended 'libpebblecommon\.sailfish' "$proxy_config" \
    'stale direct Sailfish proxy entry'

# A failed migration write must defer completion, never discard old state by
# writing its marker. Multi-key compatibility lists need one checked write,
# and imported watches must not receive a transient object ID.
require_fixed 'settings.setChecked(MARKER, COMPLETE)' "$legacy_importer" \
    'checked migration completion marker'
require_fixed 'settings.watchObjectIdChecked' "$legacy_importer" \
    'checked imported watch ID'
require_fixed 'ensureDelimitedListEntryChecked' "$legacy_importer" \
    'atomic compatibility-list migration'
require_fixed 'catch (e: CancellationException)' "$legacy_importer" \
    'legacy-source retry guard preserves cancellation'
require_fixed 'catch (e: Exception)' "$legacy_importer" \
    'legacy-source I/O failure retry guard'
require_fixed 'eligibleAddresses = knownByAddress.keys + bondedAddresses' "$legacy_importer" \
    'eligible legacy account credential sources'
require_fixed 'val values = linkedSetOf<String>()' "$legacy_importer" \
    'deduplicated legacy account credential candidates'
require_fixed 'if (candidates.size > 1)' "$legacy_importer" \
    'conflicting legacy account credential rejection'
require_fixed 'readIniChecked(directory.resolve("timeline/sync.ini"))' "$legacy_importer" \
    'retryable legacy account credential read'
require_fixed 'Files.notExists(path, LinkOption.NOFOLLOW_LINKS)' "$legacy_importer" \
    'missing versus unreadable legacy credential distinction'
require_fixed 'private const val WEATHER_MARKER = "migration.rockpoold.weather-locations.v1"' \
    "$legacy_importer" 'dedicated legacy weather-location migration marker'
require_fixed 'settings.updatePrefixChecked(ROCKWORK_WEATHER_SETTINGS_PREFIX)' \
    "$legacy_importer" 'atomic current-state-preserving weather migration install'
require_fixed 'count in 0..ROCKWORK_MAX_WEATHER_LOCATIONS' "$legacy_importer" \
    'bounded legacy weather array before materialization'
require_fixed 'legacyGlobalSettings.reconcileIfNeeded(legacyImporter.isOriginalImportComplete())' \
    "$daemon_main" 'legacy global migration independent of weather completion'
reject_extended 'legacyGlobalSettings\.reconcileIfNeeded\(legacyImporter\.isComplete\(\)\)' \
    "$daemon_main" 'weather migration must not block unrelated global migration'
require_fixed 'rockworkService.reloadWeatherSettings()' "$daemon_main" \
    'late legacy weather migration reload wiring'
require_fixed 'if (changed) weatherAutoRefresh.trigger()' "$compat_service" \
    'weather refresh only after a changed persisted snapshot'
if ! awk '
    /private fun readIni\(path: Path\)/ { in_method = 1 }
    in_method && /\.getOrElse/ && !failure { failure = NR }
    in_method && /writeFailed = true/ && !deferred { deferred = NR }
    in_method && /emptyMap\(\)/ { empty = NR; exit }
    END { exit !(failure && failure < deferred && deferred < empty) }
' "$legacy_importer"; then
    fail "failed per-watch legacy INI reads must defer migration completion in $legacy_importer"
fi
reject_extended 'firstOrNull[[:space:]]*\{[[:space:]]*it\.isNotBlank\(\)[[:space:]]*\}' \
    "$legacy_importer" 'filesystem-order legacy account credential selection'
for account_import_regression in \
    'imports a unanimous token from eligible legacy watch directories' \
    'conflicting eligible legacy tokens require explicit sign-in but complete migration' \
    'preserves an existing current OAuth token' \
    'preserves an existing empty current OAuth token' \
    'ignores OAuth tokens outside address-shaped legacy directories' \
    'retries an eligible account import after a non-regular sync file is repaired' \
    'retries account import after account-token persistence failure'
do
    require_fixed "fun \`$account_import_regression\`" "$legacy_importer_account_test" \
        "legacy account importer regression $account_import_regression"
done
for importer_retry_regression in \
    'retries when bonded-address lookup fails' \
    'retries when legacy root disappears during import' \
    'retries each non-regular per-watch INI source after repair' \
    'missing per-watch INI sources are benign'
do
    require_fixed "fun \`$importer_retry_regression\`()" "$legacy_importer_timeline_test" \
        "legacy-state importer retry regression $importer_retry_regression"
done
for weather_import_regression in \
    'imports one physical weather fixture after the original migration completed' \
    'conflicting physical weather locations complete without choosing either source' \
    'an unmatched weather directory defers then imports after its watch is eligible' \
    'an oversized physical weather array is rejected without allocation'
do
    require_fixed "fun \`$weather_import_regression\`()" "$legacy_importer_weather_test" \
        "legacy weather-location importer regression $weather_import_regression"
done
require_fixed 'fun `late persisted migration reloads and publishes the complete snapshot`()' \
    "$compat_weather_test" 'late weather migration reload publication regression'
require_fixed 'fun `unchanged persisted settings do not publish another snapshot`()' \
    "$compat_weather_test" 'unchanged weather reload suppression regression'
reject_extended 'settings\.set\(' "$legacy_importer" \
    'unchecked migration settings write'

# The old daemon stored calendar and health preferences per watch, while the
# replacement stores one account-global value. Reconcile only after v1 has
# associated every legacy directory, preserve existing/current raw state, and
# leave source-specific compatibility replies out of the generic global list.
require_fixed 'val legacyGlobalSettings = LegacyGlobalSettingsReconciler(' "$daemon_main" \
    'legacy global-settings startup coordinator'
require_fixed 'legacyGlobalSettings.reconcileIfNeeded(legacyImporter.isOriginalImportComplete())' \
    "$daemon_main" 'legacy global-settings retry wiring'
require_fixed 'if (!legacyImporter.isComplete() || !legacyGlobalSettings.isComplete())' \
    "$daemon_main" 'combined legacy migration retry gate'
require_fixed 'configGeneratedFromDefault = concreteLibPebble.configGeneratedFromDefault,' \
    "$daemon_main" 'legacy calendar config freshness capture'
require_fixed 'healthSettings.initializeIfAbsent(' "$daemon_main" \
    'coordinated absent-only legacy health initializer'
require_fixed 'configMutations.mutate { config ->' "$legacy_global_settings" \
    'serialized legacy calendar projection'
require_fixed '!configGeneratedFromDefault -> {' "$legacy_global_settings" \
    'persisted calendar config wins legacy import'
require_fixed 'settings.putBoolean(CONFIG_GENERATED_FROM_DEFAULT_KEY, true)' \
    "$libpebble3_config" 'config origin persisted before generated defaults'
require_fixed 'internal val configGeneratedFromDefault: Boolean' \
    "$libpebble3_config" 'durable config-origin classification'
require_fixed 'val configGeneratedFromDefault: Boolean' \
    "$libpebble3_connection" 'concrete config-origin seam'
reject_extended 'CONFIG_UNCLAIMED_SETTING|config-unclaimed' "$legacy_global_settings" \
    'late external config-origin marker'
require_fixed 'if (watchConfig.calendarPins == enabled) this else copy(' \
    "$legacy_global_settings" 'no-op canonical calendar projection'
require_fixed 'initializeHealthSettingsIfAbsent { current ->' "$legacy_global_settings" \
    'absent-only legacy health initialization'
require_fixed 'UNITS_OUTCOME to health.unitsOutcome' "$legacy_global_settings" \
    'independent legacy units outcome'
require_fixed 'if (!values.keys.containsAll(HEALTH_FIELDS)) return null' \
    "$legacy_global_settings" 'complete legacy health record validation'
require_fixed 'CANNED_SOURCE_SCOPED = "preserved-source-scoped"' \
    "$legacy_global_settings" 'source-scoped legacy canned preservation'
require_fixed 'settings.setAllChecked(' "$legacy_global_settings" \
    'checked legacy global outcome marker'
for migration_regression in \
    'single legacy calendar and health candidate initialize global state once' \
    'conflicting legacy watches are retained without choosing either' \
    'current canonical calendar and persisted health always win' \
    'stored config not generated from defaults is authoritative' \
    'pending v1 and malformed candidates never mutate global state' \
    'preexisting persisted default calendar value is authoritative' \
    'generated default config remains eligible after a daemon restart' \
    'units migrate independently and conflicting units are retained' \
    'failed health initialization prevents a completed migration marker' \
    'invalid canonical calendar state is terminal and never rewrites config'
do
    require_fixed "fun \`$migration_regression\`()" "$legacy_global_settings_test" \
        "legacy global-settings regression $migration_regression"
done
require_fixed 'suspend fun initializeIfAbsent(' "$health_settings_coordinator" \
    'serialized health coordinator initializer'
require_fixed 'fun `fresh initialization avoids existing-row decoding and notifies listeners`()' \
    "$health_settings_coordinator_test" 'fresh health initializer regression'
for config_origin_regression in \
    absentStorageUsesAndPersistsDefaultAsFreshConfig \
    preExistingDefaultTrueIsReportedAsLoaded \
    preExistingNonDefaultValueIsReportedAndPreserved \
    generatedOriginSurvivesAnInterruptedFirstConfigSave
do
    require_fixed "fun $config_origin_regression()" "$libpebble3_config_test" \
        "config origin regression $config_origin_regression"
done
reject_tree_extended 'RockpoolSettings\\(\\)' \
    'daemon tests must use isolated RockpoolSettings paths' \
    "$libpebble3d_dir/daemon/src/test"

# Value defaults cannot prove that the health table is fresh. The concrete
# libpebble3 seam must test raw row presence and install all four records in
# one Room transaction without expanding the portable LibPebble interface.
require_fixed '@Transaction' "$libpebble3_health_dao" \
    'transactional absent-only health initialization'
require_fixed 'suspend fun setWatchSettingsTransactionally(' "$libpebble3_health_dao" \
    'transactional complete health settings writer'
require_fixed 'suspend fun initializeIfAbsent(' \
    "$libpebble3_health_dao" 'health DAO conditional initializer'
require_fixed 'transform: (HealthSettings) -> HealthSettings,' \
    "$libpebble3_health_dao" 'health DAO initializer transformation seam'
require_fixed 'if (hasPersistedHealthSettings()) return null' \
    "$libpebble3_health_dao" 'raw health rows block legacy initialization'
require_fixed '@Query("SELECT EXISTS(SELECT 1 FROM HealthSettingsEntryEntity)")' \
    "$libpebble3_health_dao" 'raw health settings presence query'
require_fixed 'internal suspend fun initializeHealthSettingsIfAbsent' "$libpebble3_health" \
    'health service conditional initializer'
require_fixed 'internal suspend fun persistHealthSettings(' "$libpebble3_health" \
    'awaited health service persistence seam'
require_fixed 'suspend fun initializeHealthSettingsIfAbsent(' \
    "$libpebble3_connection" 'concrete libpebble3 conditional initializer'
require_fixed 'suspend fun persistHealthSettings(' \
    "$libpebble3_connection" 'concrete libpebble3 durable health writer'
require_fixed '): HealthSettingsInitialization? = health.initializeHealthSettingsIfAbsent(transform)' \
    "$libpebble3_connection" 'concrete health initialization result seam'
for health_init_regression in \
    absentSettingsAreInitializedAsOneCompleteRecord \
    deliberatelyStoredDefaultsAreNotTreatedAsAbsent \
    anyPartialPersistedRecordBlocksInitialization \
    deletedPersistedRecordStillBlocksInitialization \
    completeSettingsWriteRollsBackAllRowsWhenTheLastInsertFails
do
    require_fixed "fun $health_init_regression()" "$libpebble3_health_init_test" \
        "health initialization regression $health_init_regression"
done

# Compatibility canned writes are source-keyed partial mutations. Their
# read/merge/write must share the settings monitor across every exported watch,
# validate round-trippable bounded records, and their snapshots must never leak
# into libpebble3's generic response list.
require_fixed 'fun updatePrefixChecked(' "$rockpool_settings" \
    'atomic settings prefix mutation'
require_fixed 'val validated = validateStoredList(cans)' "$compat_pebble_object" \
    'compatibility canned input validation'
require_fixed 'mergeStoredList("canned", validated)' "$compat_pebble_object" \
    'atomic compatibility canned merge'
require_fixed 'return settings.updatePrefixChecked(prefix) {' "$compat_pebble_object" \
    'shared settings monitor for compatibility canned merge'
require_fixed 'encodeStoredList(prefix, decodeStoredValues(prefix, stored) + values)' \
    "$compat_pebble_object" 'source-keyed compatibility canned partial merge'
require_fixed 'private fun validateNormalizedStoredList(' "$compat_pebble_object" \
    'compatibility canned round-trip validation'
require_fixed 'settings.entries(prefix)' "$compat_pebble_object" \
    'coherent compatibility canned/contact snapshot'
require_fixed 'fun `atomic prefix updates cannot lose a concurrent record`()' \
    "$rockpool_settings_test" 'atomic settings prefix regression'
require_fixed 'fun `saving one canned response group preserves every other group`()' \
    "$compat_mutation_signal_test" 'compatibility canned partial-merge regression'
require_fixed 'fun `canned responses and contacts reject records that cannot round trip`()' \
    "$compat_mutation_signal_test" 'compatibility canned validation regression'
require_fixed 'fun `valid canned update repairs malformed persisted groups`()' \
    "$compat_mutation_signal_test" 'compatibility canned repair regression'
require_fixed 'if (validStoredListGroup(name, entries)) put(name, entries)' \
    "$compat_pebble_object" 'malformed compatibility canned filtering'
require_fixed 'assertEquals(originalNotificationConfig, libPebble.config.value.notificationConfig)' \
    "$compat_mutation_signal_test" 'compatibility canned isolation regression'

# The old C++ daemon is deliberately deleted. Empty directories may remain in
# a working tree after deletion, but no source or service file may survive.
if [ -d "$project_dir/rockworkd" ] && \
    find "$project_dir/rockworkd" -type f -print -quit | grep -q .
then
    fail "retired rockworkd source remains"
fi
reject_extended '(^|[[:space:]])rockworkd([[:space:]]|$)' "$project_dir/rockpool.pro" \
    'stale rockworkd build target'

# Firmware-update state is emitted asynchronously. The static compatibility
# proxy must route it to the real refresh member; forwarding it to a nonexistent
# Qt signal leaves the upgrade spinner/menu stale.
require_fixed '&RockworkPebbleInterface::UpgradingFirmwareChanged' \
    "$rockwork_pebble" 'typed firmware-upgrade compatibility signal'
require_fixed 'this, &Pebble::refreshFirmwareUpdateInfo' \
    "$rockwork_pebble" 'firmware-upgrade compatibility refresh member'
reject_extended 'SIGNAL\(refreshFirmwareUpdateInfo\(\)\)' \
    "$rockwork_pebble" 'firmware-upgrade refresh routed to a nonexistent signal'

# Keep the supported weather units setting durable. The retired providers were
# the only source of localized condition strings, so their now-ineffective
# language selector must not remain in the active UI.
for weather_setting in \
    'if(root.units !== initialUnits)' \
    'pebble.weatherUnits = root.units;'
do
    require_fixed "$weather_setting" "$weather_settings_dialog" \
        "weather unit persistence $weather_setting"
done
require_fixed 'canAccept: settingsEditable && dirty' "$weather_settings_dialog" \
    'weather dialog readiness and dirty-state acceptance gate'
require_fixed 'pebble.refreshWeatherSettings()' "$weather_settings_dialog" \
    'lazy asynchronous weather settings refresh'
require_fixed 'var locStore = locations.count !== locStash.length;' \
    "$weather_settings_dialog" 'weather location removal persistence'
reject_extended 'Alternate Provider|Provider Key|weatherApiKey|WeatherAltKey|weatherLanguage|boxLang|modLang' \
    "$weather_settings_dialog" \
    'unsupported weather-provider controls in the active compatibility UI'
require_fixed 'fun InjectWeatherData(locationName: String, conditions: Map<String, Variant<*>>)' \
    "$compat_pebble_object" 'external weather injection compatibility endpoint'
reject_extended 'onTextEdited' "$weather_settings_dialog" \
    'unsupported Sailfish Silica TextField textEdited handler'
reject_extended 'autocomplete\.wunderground\.com|http://' "$location_picker" \
    'retired or insecure weather location search endpoint'
require_fixed 'https://geocoding-api.open-meteo.com/v1/search' "$location_picker" \
    'supported keyless weather location search endpoint'
require_fixed 'encodeURIComponent(query)' "$location_picker" \
    'weather location query escaping'
require_fixed 'generation !== searchGeneration || request !== activeRequest' \
    "$location_picker" 'stale weather location reply rejection'
require_fixed 'Location search and forecasts by <a href=\"https://open-meteo.com/\">Open-Meteo</a>' \
    "$location_picker" 'weather location provider attribution'
require_fixed 'Location search and forecasts by <a href=\"https://open-meteo.com/\">Open-Meteo</a>' \
    "$weather_settings_dialog" 'automatic weather provider attribution'
require_fixed 'https://api.open-meteo.com/v1/forecast' "$libpebble3_open_meteo" \
    'supported keyless weather forecast endpoint'
require_fixed 'parameters.append("forecast_days", "2")' "$libpebble3_open_meteo" \
    'bounded two-day weather forecast request'
require_fixed 'source == RockworkWeatherObservationSource.EXTERNAL' "$compat_weather" \
    'external weather injection precedence'
require_fixed 'weatherAutoRefresh.start()' "$compat_service" \
    'automatic weather refresh service lifecycle'
require_fixed 'refreshWeather = weatherAutoRefresh::trigger' "$compat_service" \
    'weather setting change refresh wiring'
for weather_regression in \
    'external injection wins over an in-flight automatic result' \
    'automatic refresh failures preserve the last durable observation' \
    'coordinate change removes a prior automatic observation'
do
    require_fixed "$weather_regression" "$compat_weather_refresh_test" \
        "automatic weather regression $weather_regression"
done

require_fixed 'root.pebble.refreshSettingsPage()' "$settings_page" \
    'lazy asynchronous compatibility settings refresh'
require_fixed 'property bool settingsReady: pebble && pebble.settingsPageReady' \
    "$settings_page" 'compatibility settings readiness gate'
require_fixed 'automaticCheck: false' "$settings_page" \
    'calendar switch remains owned by its authoritative cache'
require_fixed 'checked: root.pebble ? root.pebble.syncAppsFromCloud : false' \
    "$settings_page" 'cloud-app sync switch uses its asynchronous cache'
require_fixed '!root.pebble.syncAppsFromCloud' "$settings_page" \
    'cloud-app sync switch writes only on user activation'
reject_extended 'onCurrentIndexChanged' "$settings_page" \
    'compatibility settings cache update echoed as a write'
require_fixed 'root.pebble.refreshCannedResponses()' "$settings_page" \
    'lazy asynchronous canned-response refresh'
require_fixed 'root.cannedResponsesReady ? Object.keys(cannedResponses) : []' \
    "$settings_page" 'canned-response readiness gate'
require_fixed 'cannedResponses[modelData].slice(0)' "$settings_page" \
    'detached canned-response editor input'
require_fixed 'pebble.refreshCannedResponses()' "$send_text_settings_dialog" \
    'asynchronous Send Text response refresh'
require_fixed '(responses[msgKey] || []).slice(0)' "$send_text_settings_dialog" \
    'detached Send Text response editor input'
require_fixed 'pebble.refreshCannedContacts()' "$send_text_settings_dialog" \
    'asynchronous Send Text favorite-contact refresh'
require_fixed 'property bool cannedContactsReady: pebble && pebble.cannedContactsReady' \
    "$send_text_settings_dialog" 'favorite-contact readiness gate'
require_fixed 'var snapshot = cloneContacts(pebble.getCannedContacts([]));' \
    "$send_text_settings_dialog" 'cache-only favorite-contact snapshot'
require_fixed 'var updated = root.cloneContacts(oldModel.src);' \
    "$send_text_settings_dialog" 'copy-on-write favorite-contact deletion'
require_fixed 'contacts = cloneContacts(oldModel.src);' \
    "$send_text_settings_dialog" 'detached favorite-contact save snapshot'
reject_extended 'oldModel\.src\[[^]]+\]\.splice|delete oldModel\.src' \
    "$send_text_settings_dialog" 'in-place favorite-contact cache mutation'
for copy_on_write in \
    'cans[source] = list.slice(0);' \
    'var updated = root.list.slice(0);' \
    'var inserted = root.list.slice(0);' \
    'root.list = updated;' \
    'root.list = inserted;'
do
    require_fixed "$copy_on_write" "$responses_page" \
        "copy-on-write canned-response edit $copy_on_write"
done
reject_extended 'root\.list\[[^]]+\][[:space:]]*=|root\.list\.splice|rspList\.model[[:space:]]*=' \
    "$responses_page" 'in-place canned-response cache mutation'

require_fixed 'root.pebble.refreshDeveloperSettings()' "$developer_tools_page" \
    'lazy asynchronous developer-tools settings refresh'
require_fixed 'root.pebble.refreshDeveloperSettings()' "$main_menu_page" \
    'asynchronous main-menu developer-state refresh'
require_fixed 'root.pebble.developerSettingsReady' "$main_menu_page" \
    'main-menu developer-state readiness gate'
require_fixed 'root.pebble && root.pebble.developerSettingsReady' \
    "$developer_tools_page" 'developer-tools readiness gate'
require_fixed 'automaticCheck: false' "$developer_tools_page" \
    'developer-tools cache-owned switch state'
reject_extended 'onCheckedChanged:.*devConnEnabled' "$developer_tools_page" \
    'developer-connection cache update echoed as a write'

require_fixed 'root.pebble.refreshTimelineColors()' "$notifications_page" \
    'asynchronous notification colour-palette refresh'
require_fixed 'root.pebble.timelineColorsReady' "$notifications_page" \
    'notification colour-map readiness gate'
require_fixed 'root.pebble.refreshTimelineColors()' "$notification_color_page" \
    'asynchronous notification colour-picker refresh'
require_fixed 'root.pebble.refreshTimelineIcons()' "$notification_icon_page" \
    'asynchronous notification icon-picker refresh'
require_fixed 'onIconsChanged: updateFilter()' "$notification_icon_page" \
    'late icon-palette filter rebuild'
for palette_page in \
    "$notifications_page" "$notification_color_page" "$notification_icon_page"
do
    reject_extended 'timeline(Colors|Icons)\(\)' "$palette_page" \
        'blocking notification palette method call from QML'
done

if ! awk '
    index($0, "root.pebble.setTimelineWindow(") { apply = NR }
    apply && !start && index($0, "Number(timelineWindowStartField.text)") { start = NR }
    apply && !fade && index($0, "Number(timelineWindowFadeField.text)") { fade = NR }
    apply && !end && index($0, "Number(timelineWindowEndField.text)") { end = NR }
    END { exit !(apply && apply < start && start < fade && fade < end) }
' "$settings_page"
then
    fail "timeline editor does not pass typed draft values directly in $settings_page"
fi

require_fixed 'Q_PROPERTY(QString address READ address NOTIFY identityChanged)' \
    "$rockwork_pebble_header" 'asynchronously populated watch address exposed to Rockwork QML'
require_fixed 'pebble.address.length > 0' "$cover_page" \
    'empty-address reconnect guard'
require_fixed 'rockPool.connectWatch(pebble.address);' "$cover_page" \
    'cover reconnect routed through the watch manager'
reject_extended 'pebble\.reconnect\(' "$cover_page" \
    'undefined Pebble reconnect call'
require_fixed 'function connectWatch(address)' "$rockpool_qml" \
    'cover-visible watch-manager reconnect bridge'
require_fixed 'pebbles.connectWatch(address)' "$rockpool_qml" \
    'watch-manager reconnect delegation'
require_fixed 'contentType: "image/png"' "$screenshots_page" \
    'PNG screenshot sharing content type'
reject_extended 'contentType:[[:space:]]*"image/jpeg"' "$screenshots_page" \
    'JPEG declaration for PNG screenshots'

# Opening the main page while disconnected makes its initial screenshot request
# ineligible. Retry on the existing connection signal so the watch preview can
# recover without recreating the page, while avoiding a known futile request.
require_fixed 'onConnectedChanged: {' "$main_menu_page" \
    'main-menu reconnect screenshot handler'
if ! awk '
    /onConnectedChanged:[[:space:]]*\{/ { in_handler = 1 }
    in_handler && /root\.pebble\.connected[[:space:]]*&&[[:space:]]*!root\.pebble\.screenshots\.latestScreenshot/ {
        guarded = 1
    }
    in_handler && /root\.pebble\.requestScreenshot\(\)/ { requested = 1 }
    in_handler && /^([[:space:]]*)\}/ && guarded && requested { complete = 1 }
    END { exit !(guarded && requested && complete) }
' "$main_menu_page"
then
    fail "main-menu screenshot is not retried after reconnect in $main_menu_page"
fi

# ServiceControl is constructed during the first UI frame.  Keep systemd
# subscription, unit lookup/state reads and enable/start/stop chains off the
# GUI thread, serialize competing chains, and never let a stale owner/property
# reply make the startup gate authoritative.
require_fixed 'class SystemdManagerInterface : public QDBusAbstractInterface' \
    "$service_control_header" 'static non-introspecting systemd manager proxy'
require_fixed 'Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)' \
    "$service_control_header" 'asynchronous systemd startup readiness'
require_fixed 'm_queuedOperation' "$service_control_header" \
    'serialized queued systemd operation state'
require_fixed 'm_systemd->asyncCall(QStringLiteral("Subscribe"))' \
    "$service_control" 'asynchronous systemd subscription'
require_fixed 'm_systemd->asyncCall(QStringLiteral("LoadUnit"), ROCKPOOLD_SYSTEMD_UNIT)' \
    "$service_control" 'asynchronous systemd unit lookup'
require_fixed 'm_unitPropertiesInterface->asyncCall(QStringLiteral("GetAll")' \
    "$service_control" 'asynchronous systemd unit-state read'
require_fixed 'm_systemd->asyncCallWithArgumentList(method, arguments)' \
    "$service_control" 'asynchronous serialized systemd operation'
require_fixed 'state == QStringLiteral("activating")' "$service_control" \
    'in-progress systemd start treated as running'
require_fixed 'emit serviceRunningChanged();' "$service_control" \
    'service-running Q_PROPERTY notifier'
require_fixed 'if (changed.contains(QStringLiteral("ActiveState")))' "$service_control" \
    'normal systemd ActiveState change handling'
require_fixed 'proxyServiceEpoch != m_serviceEpoch' "$service_control" \
    'stale systemd owner signal rejection'
require_fixed '!serviceController.ready || serviceController.initialStateHandled' \
    "$rockpool_qml" 'authoritative systemd startup gate'
reject_extended 'emit[[:space:]]+serviceRunning\(\)' "$service_control" \
    'service-running getter called instead of its notifier'
reject_extended 'QDBusError[[:space:]]+reply' "$service_control" \
    'default-constructed service-control error checked instead of method replies'
reject_extended '(m_systemd|m_unitPropertiesInterface)->call\(' "$service_control" \
    'blocking service-control GUI-thread D-Bus call'
for regression in \
    constructorDoesNotBlockOnDelayedBootstrap activeStateSignalBeatsStaleGetAll \
    lateOldOwnerPropertiesSignalIsIgnored \
    startChainIsAsyncAndOrdered stopChainIsAsyncAndOrdered restartIsAsync \
    stopRequestedDuringStartWaitsForStartChain
do
    require_fixed "void ServiceControlAsyncTest::$regression()" \
        "$rockwork_servicecontrol_async_test" \
        "asynchronous service-control regression $regression"
done

# Built-in settings pages own their complete configuration flow.  In
# particular Weather has no accepted callback, but must still avoid falling
# through to the generic PKJS ConfigurationURL request.
require_fixed 'if(popup) {' "$installed_apps_page" \
    'built-in app-settings popup handling'
require_fixed 'if(cbacc) {' "$installed_apps_page" \
    'optional built-in app-settings acceptance callback'
reject_extended 'if[[:space:]]*\(popup[[:space:]]*&&[[:space:]]*cbacc\)' \
    "$installed_apps_page" 'built-in settings fall-through when no callback exists'

# App-store compatibility is keyed by hardware platform. Keep every model the
# active UI and backend know about visible on the upgrade page.
for platform in aplite basalt chalk diorite emery flint gabbro
do
    require_fixed "key: \"$platform\"" "$app_upgrade_page" \
        "$platform app-upgrade compatibility entry"
done

# Model refresh can delete the app object before AppUpgrade receives the
# resulting change signal. Keep every binding safe until the page pops.
require_fixed 'property int app_index: -1' "$app_upgrade_page" \
    'safe app-upgrade index before page parameters arrive'
require_fixed 'app_model && app_index >= 0 ? app_model.get(app_index) : null' \
    "$app_upgrade_page" 'null-safe app-upgrade model lookup'
require_fixed 'enabled: root.appInstallationAllowed && root.app && !installing && !root.app.companion' \
    "$app_upgrade_page" 'null-safe app-upgrade action'
reject_extended 'property var app:[[:space:]]*app_model\.get\(app_index\)' \
    "$app_upgrade_page" 'unguarded app-upgrade model lookup'

# latestScreenshot is the first row. Removing it, or clearing a non-empty
# model, must notify QML so the main-page preview advances or disappears.
if [ "$(grep -c 'emit latestScreenshotChanged();' "$rockwork_screenshot_model")" -lt 3 ]
then
    fail "latest screenshot changes are not notified for every model mutation in $rockwork_screenshot_model"
fi
require_fixed 'const bool latestChanged = !m_files.isEmpty();' \
    "$rockwork_screenshot_model" 'non-empty screenshot clear notification'
require_fixed 'const bool latestChanged = idx == 0;' \
    "$rockwork_screenshot_model" 'latest screenshot removal notification'

# Newly learned notification applications and primary/compatibility filter
# mutations must update every already-open Rockwork model without reconnecting.
require_fixed 'watchNotificationApps()' "$compat_service" \
    'live compatibility notification-app observer'
require_fixed 'notificationFilters.addListener' "$compat_service" \
    'cross-API notification-filter observer'
require_fixed 'RockworkNotificationSourceTracker' "$compat_notification_sources" \
    'notification source addition/change/removal diff'
require_fixed 'colorName: String?' "$compat_notification_sources" \
    'notification appearance included in source invalidation state'
require_fixed 'RockworkNotificationAppearanceCoordinator' "$compat_notification_appearance" \
    'serialized combined notification-appearance update'
require_fixed 'Channel<Request>(Channel.UNLIMITED)' "$compat_notification_mutations" \
    'FIFO compatibility notification-filter mutation queue'
require_fixed 'notificationFilterMutations = notificationFilterMutations' "$compat_service" \
    'service-wide compatibility notification-filter mutation queue'
require_fixed 'this, &Pebble::notificationAppearanceReplyFinished' "$rockwork_pebble" \
    'notification-appearance asynchronous reply handling'
require_fixed 'refreshNotificationsAsync();' "$rockwork_pebble" \
    'authoritative notification-state refresh'
require_fixed 'm_notifications->setColorName(sourceId, colorName);' "$rockwork_pebble" \
    'non-destructive optimistic notification colour update'
require_fixed 'm_notifications->setIconCode(sourceId, iconCode);' "$rockwork_pebble" \
    'non-destructive optimistic notification icon update'
require_fixed 'roles.append(RoleName);' "$rockwork_notification_model" \
    'existing notification source name refresh'
require_fixed 'roles.append(RoleIcon);' "$rockwork_notification_model" \
    'existing notification source icon refresh'
reject_extended 'notificationsFilter\(\)\.value\(sourceId\)' "$rockwork_pebble" \
    'synchronous stale notification appearance reread'
require_fixed 'if (!m_connected) {' "$rockwork_pebble" \
    'edge-triggered compatibility connection notification'
if ! awk '
    /void Pebble::pebbleConnected\(\)/ { in_connected = 1 }
    in_connected && /dataChanged\(\)/ { refreshed = 1 }
    in_connected && /if \(!m_connected\)/ { guarded = 1 }
    in_connected && /emit connectedChanged\(\)/ { notified = 1 }
    in_connected && /^}/ { exit !(refreshed && guarded && notified) }
    END { if (!in_connected) exit 1 }
' "$rockwork_pebble"
then
    fail "Pebble connected transition can emit duplicate notifications in $rockwork_pebble"
fi

# Sorting the raw pointer list after begin/endInsertRows without a layout/reset
# signal corrupts QAbstractItemModel row identity. Reorder only through the
# explicit model-aware helper, including on connection-state changes.
require_fixed 'void Pebbles::resortPebbles()' "$rockwork_pebbles" \
    'model-aware Rockwork watch sorting'
require_fixed 'beginResetModel();' "$rockwork_pebbles" \
    'watch-list ordering reset notification'
require_fixed 'resortPebbles();' "$rockwork_pebbles" \
    'watch-list connection-state resort'
reject_extended 'std::sort\(m_pebbles\.begin\(\),[[:space:]]*m_pebbles\.end\(\)' \
    "$rockwork_pebbles" 'unannounced direct watch-list model sort'

# Manager discovery, scanning and commands run on the GUI thread. Every bus
# request must be asynchronous, and replies from an older request or daemon
# owner must not repopulate the model after a restart.
require_fixed 'QDBusConnection::sessionBus().asyncCall(message)' \
    "$rockwork_pebbles" 'asynchronous compatibility-manager D-Bus call'
for epoch in m_serviceEpoch m_watchListEpoch m_versionEpoch m_scanningEpoch m_scanResultsEpoch
do
    require_fixed "$epoch" "$rockwork_pebbles_header" \
        "compatibility-manager stale-reply epoch $epoch"
done
for completion in \
    watchListReplyFinished versionReplyFinished scanningReplyFinished \
    scanResultsReplyFinished managerCommandReplyFinished
do
    require_fixed "Pebbles::$completion" "$rockwork_pebbles" \
        "asynchronous compatibility-manager completion $completion"
done
for command in StartScan StopScan ConnectWatch DisconnectWatch ForgetWatch
do
    require_fixed "sendManagerCommand(QStringLiteral(\"$command\")" \
        "$rockwork_pebbles" "asynchronous compatibility-manager command $command"
done
require_fixed 'serviceEpoch != m_serviceEpoch || requestEpoch != m_watchListEpoch' \
    "$rockwork_pebbles" 'stale watch-list reply rejection'
require_fixed 'serviceEpoch != m_serviceEpoch || requestEpoch != m_scanResultsEpoch' \
    "$rockwork_pebbles" 'stale scan-result reply rejection'
require_fixed '&QDBusServiceWatcher::serviceOwnerChanged' "$rockwork_pebbles" \
    'direct compatibility-service owner replacement handling'
require_fixed 'Q_PROPERTY(QString version READ version NOTIFY versionChanged)' \
    "$rockwork_pebbles_header" 'asynchronously cached compatibility version'
reject_extended '\.call\(' "$rockwork_pebbles" \
    'blocking compatibility-manager GUI-thread D-Bus call'
manager_signal_hooks=$(grep -c 'QDBusConnection::sessionBus().connect' "$rockwork_pebbles")
if [ "$manager_signal_hooks" -ne 3 ]; then
    fail "compatibility-manager signals must be installed exactly once in $rockwork_pebbles"
fi
require_fixed 'QDBusConnectionInterface::ReplaceExistingService' \
    "$rockwork_pebbles_async_test" 'direct compatibility-service owner replacement regression'
require_fixed 'replyPending(QStringLiteral("ScanResults"), 1' \
    "$rockwork_pebbles_async_test" 'out-of-order scan-result regression'
require_fixed 'void PebblesAsyncTest::commandsDoNotWaitForReplies()' \
    "$rockwork_pebbles_async_test" 'nonblocking compatibility-manager command regression'
require_fixed 'exec dbus-run-session -- "$@"' "$rockwork_pebbles_async_runner" \
    'isolated Rockwork manager regression bus'
if [ ! -x "$rockwork_pebbles_async_runner" ]; then
    fail "Rockwork private-bus test runner is not executable: $rockwork_pebbles_async_runner"
fi

# A watch object is constructed on the GUI thread for every returned path.
# Keep its identity, models, screenshots and firmware snapshot asynchronous,
# reject replies from older requests/owners, and expose late identity through
# proper property/model notifiers.
require_fixed 'class RockworkPebbleInterface : public QDBusAbstractInterface' \
    "$rockwork_pebble_header" 'static non-introspecting compatibility watch proxy'
reject_extended 'new QDBusInterface' "$rockwork_pebble" \
    'dynamic compatibility proxy introspection on the GUI thread'
require_fixed 'Q_PROPERTY(QString name READ name NOTIFY identityChanged)' \
    "$rockwork_pebble_header" 'asynchronously populated watch name'
for epoch in \
    m_serviceEpoch m_connectionEpoch m_appsEpoch m_screenshotsEpoch \
    m_firmwareEpoch m_notificationFiltersEpoch m_weatherLocationsEpoch \
    m_weatherWriteEpochs m_weatherValueRevisions \
    m_developerWriteEpochs m_developerValueRevisions \
    m_notificationFilterCommandEpochs \
    m_timelineColorsEpoch m_timelineIconsEpoch
do
    require_fixed "$epoch" "$rockwork_pebble_header" \
        "compatibility-watch stale-reply epoch $epoch"
done
for completion in \
    propertyReplyFinished connectionPropertyReplyFinished appsReplyFinished \
    notificationFiltersReplyFinished screenshotsReplyFinished \
    notificationFilterCommandReplyFinished \
    firmwarePropertyReplyFinished weatherLocationsReplyFinished \
    weatherWriteReplyFinished developerWriteReplyFinished \
    dumpLogsReplyFinished timelinePaletteReplyFinished
do
    require_fixed "Pebble::$completion" "$rockwork_pebble" \
        "asynchronous compatibility-watch completion $completion"
done
for method in InstalledApps NotificationsFilter Screenshots
do
    require_fixed "m_iface->asyncCall(QStringLiteral(\"$method\"))" \
        "$rockwork_pebble" "asynchronous compatibility-watch $method snapshot"
done
require_fixed 'foreach (const QString &propertyName, firmwareProperties())' \
    "$rockwork_pebble" 'atomic asynchronous firmware snapshot'
require_fixed 'return m_notificationFilters;' "$rockwork_pebble" \
    'cached nonblocking notification-filter property'
require_fixed 'Q_PROPERTY(bool weatherSettingsReady READ weatherSettingsReady NOTIFY weatherSettingsReadyChanged)' \
    "$rockwork_pebble_header" 'atomic asynchronous weather settings readiness'
for weather_cache in \
    'return m_weatherLocations;' 'return m_weatherUnits;' \
    'return m_weatherLanguage;' 'return m_weatherAltKey;'
do
    require_fixed "$weather_cache" "$rockwork_pebble" \
        "cached nonblocking weather property $weather_cache"
done
require_fixed 'm_iface->asyncCallWithArgumentList(method, QVariantList() << value)' \
    "$rockwork_pebble" 'asynchronous weather setting write'
require_fixed 'valueRevision == m_weatherValueRevisions.value(propertyName)' \
    "$rockwork_pebble" 'weather write failure rollback ordering'
require_fixed '&RockworkPebbleInterface::WeatherLocationsChanged' \
    "$rockwork_pebble" 'authoritative weather-location invalidation signal'
require_fixed 'Q_PROPERTY(bool developerSettingsReady READ developerSettingsReady NOTIFY developerSettingsReadyChanged)' \
    "$rockwork_pebble_header" 'atomic asynchronous developer settings readiness'
for developer_cache in \
    'return m_devConnEnabled;' 'return m_devConnServerRunning;' \
    'return m_logLevel;'
do
    require_fixed "$developer_cache" "$rockwork_pebble" \
        "cached nonblocking developer property $developer_cache"
done
require_fixed 'requestProperty(QString::fromLatin1(DEV_CONNECTION_STATE));' \
    "$rockwork_pebble" 'authoritative developer-connection signal readback'
require_fixed 'm_iface->asyncCallWithArgumentList(QStringLiteral("DumpLogs"),' \
    "$rockwork_pebble" 'asynchronous developer log export request'
require_fixed 'm_logDumpPending' "$rockwork_pebble_header" \
    'single in-flight compatibility log export'
require_fixed 'logDumpWasPending' "$rockwork_pebble" \
    'service-owner loss releases compatibility log export UI'
require_fixed 'Q_PROPERTY(QVariantList timelineColors READ timelineColors NOTIFY timelineColorsChanged)' \
    "$rockwork_pebble_header" 'cached notification colour palette'
require_fixed 'Q_PROPERTY(QVariantList timelineIcons READ timelineIcons NOTIFY timelineIconsChanged)' \
    "$rockwork_pebble_header" 'cached notification icon palette'
require_fixed 'return m_timelineColors;' "$rockwork_pebble" \
    'nonblocking notification colour-palette getter'
require_fixed 'return m_timelineIcons;' "$rockwork_pebble" \
    'nonblocking notification icon-palette getter'
require_fixed 'm_iface->asyncCall(method)' "$rockwork_pebble" \
    'asynchronous notification palette request'
require_fixed 'sendNotificationFilterCommand(' "$rockwork_pebble" \
    'asynchronous notification filter command'
require_fixed 'QTimer::singleShot(250, this' "$rockwork_pebble" \
    'bounded notification palette retry'
require_fixed 'm_timelineColorsFailures' "$rockwork_pebble_header" \
    'bounded notification colour-palette failure state'
require_fixed 'm_timelineIconsFailures' "$rockwork_pebble_header" \
    'bounded notification icon-palette failure state'
require_fixed 'Q_PROPERTY(bool settingsPageReady READ settingsPageReady NOTIFY settingsPageReadyChanged)' \
    "$rockwork_pebble_header" 'atomic asynchronous compatibility settings readiness'
for settings_cache in \
    'return m_imperialUnits;' 'return m_profileWhenConnected;' \
    'return m_profileWhenDisconnected;' 'return m_calendarSyncEnabled;' \
    'return m_syncAppsFromCloud;'
do
    require_fixed "$settings_cache" "$rockwork_pebble" \
        "cached nonblocking compatibility setting $settings_cache"
done
require_fixed 'm_settingsWriteEpochs' "$rockwork_pebble_header" \
    'compatibility settings write ordering'
require_fixed 'm_settingsAuthoritativeProperties' "$rockwork_pebble_header" \
    'compatibility settings authoritative-value tracking'
require_fixed 'valueRevision == m_settingsValueRevisions.value(propertyName)' \
    "$rockwork_pebble" 'compatibility settings write rollback ordering'
require_fixed 'Pebble::settingsPropertyChangedFromService' "$rockwork_pebble" \
    'authoritative compatibility settings signal readback'
require_fixed 'Pebble::settingsPropertyReadFailed' "$rockwork_pebble" \
    'bounded compatibility settings read failure handling'
require_fixed 'markSettingsPropertyLoaded(propertyName, false)' "$rockwork_pebble" \
    'usable non-authoritative compatibility settings fallback'
require_fixed 'Q_PROPERTY(bool cannedResponsesReady READ cannedResponsesReady NOTIFY cannedResponsesReadyChanged)' \
    "$rockwork_pebble_header" 'asynchronous canned-response readiness'
require_fixed 'return m_cannedResponses;' "$rockwork_pebble" \
    'cached nonblocking canned-response getter'
require_fixed 'requestProperty(QString::fromLatin1(CANNED_RESPONSES));' \
    "$rockwork_pebble" 'asynchronous canned-response readback'
require_fixed 'm_iface->asyncCallWithArgumentList(' "$rockwork_pebble" \
    'asynchronous compatibility map write'
require_fixed 'QStringLiteral("setCannedResponses"), QVariantList() << normalized' \
    "$rockwork_pebble" 'partial canned-response map write'
require_fixed 'm_cannedResponsesWriteEpoch' "$rockwork_pebble_header" \
    'canned-response write ordering'
require_fixed 'm_cannedResponsesAuthoritative' "$rockwork_pebble_header" \
    'canned-response fallback writeability'
require_fixed 'normalized.insert(it.key(), strings);' "$rockwork_pebble" \
    'explicit empty canned-response list retention'
require_fixed 'Q_PROPERTY(bool cannedContactsReady READ cannedContactsReady NOTIFY cannedContactsReadyChanged)' \
    "$rockwork_pebble_header" 'asynchronous favorite-contact readiness'
require_fixed 'return m_cannedContacts;' "$rockwork_pebble" \
    'cached nonblocking favorite-contact getter'
require_fixed 'QString::fromLatin1(FAVORITE_CONTACTS),' "$rockwork_pebble" \
    'asynchronous full favorite-contact read'
require_fixed 'QStringLiteral("setFavoriteContacts"),' "$rockwork_pebble" \
    'asynchronous full favorite-contact replacement'
require_fixed 'm_cannedContactsRequestEpoch' "$rockwork_pebble_header" \
    'favorite-contact read ordering'
require_fixed 'm_cannedContactsWriteEpoch' "$rockwork_pebble_header" \
    'favorite-contact write ordering'
require_fixed 'm_cannedContactsAuthoritative' "$rockwork_pebble_header" \
    'favorite-contact fallback writeability'
# Health settings are opened from a built-in app configuration action.  The
# dialog must never synchronously obtain its map or edit the Pebble cache in
# place: it loads a detached snapshot only after the lazy cache is ready.
require_fixed 'Q_PROPERTY(bool healthParamsReady READ healthParamsReady NOTIFY healthParamsReadyChanged)' \
    "$rockwork_pebble_header" 'asynchronous health-settings readiness'
require_fixed 'return m_healthParams;' "$rockwork_pebble" \
    'cached nonblocking health-settings getter'
require_fixed 'void Pebble::refreshHealthParams()' "$rockwork_pebble" \
    'lazy health-settings refresh'
require_fixed 'requestProperty(QString::fromLatin1(HEALTH_PARAMS));' "$rockwork_pebble" \
    'asynchronous health-settings read and readback'
require_fixed 'QStringLiteral("SetHealthParams")' "$rockwork_pebble" \
    'asynchronous health-settings write method'
require_fixed 'm_iface->asyncCallWithArgumentList(' "$rockwork_pebble" \
    'asynchronous health-settings write transport'
for health_state in \
    m_healthParamsRequested m_healthParamsAuthoritative m_healthParamsValid \
    m_healthParamsWriteEpoch m_healthParamsValueRevision
do
    require_fixed "$health_state" "$rockwork_pebble_header" \
        "health-settings owner/read/write state $health_state"
done
require_fixed 'this, &Pebble::healthParamsChangedFromService' "$rockwork_pebble" \
    'health-settings signal invalidation handler'
require_fixed 'pebble.refreshHealthParams()' "$health_settings_dialog" \
    'lazy asynchronous Health dialog refresh'
require_fixed 'property bool settingsReady: pebble && pebble.healthParamsReady && snapshotLoaded' \
    "$health_settings_dialog" 'Health dialog readiness gate'
require_fixed 'var params = cloneParams(pebble.healthParams);' "$health_settings_dialog" \
    'detached Health dialog cache snapshot'
require_fixed 'property var dirtyFields: ({})' "$health_settings_dialog" \
    'Health dialog per-field dirty state'
require_fixed 'function markDirty(field)' "$health_settings_dialog" \
    'Health dialog dirty-field marker'
require_fixed 'if (loadingSnapshot || !snapshotLoaded)' "$health_settings_dialog" \
    'Health dialog initialization writeback guard'
for health_field in enabled age height weight gender moreActive sleepMore
do
    require_fixed "if (!dirtyFields[\"$health_field\"])" "$health_settings_dialog" \
        "Health dialog canonical merge preserves dirty $health_field"
done
require_fixed 'var updated = cloneParams(healthParams);' "$health_settings_dialog" \
    'copy-on-write Health dialog save snapshot'
require_fixed 'pebble.healthParams = updated;' "$health_settings_dialog" \
    'Health dialog accepted settings write'
reject_extended 'healthParams:[[:space:]]*pebble\.healthParams' "$installed_apps_page" \
    'Health dialog passed the live cached map directly'
reject_extended 'fetchVarMap\("HealthParams"\)|m_iface->call\("(HealthParams|SetHealthParams)"\)' \
    "$rockwork_pebble" 'blocking compatibility health-settings call'

# Primary operations can finish before the method reply containing their path.
# The reusable client must install signal listeners before its authoritative
# GetAll, reject reordered snapshots and old service owners, and never infer a
# terminal state from the acknowledgement-only Cancel reply.
require_fixed 'rockpooloperation.h' "$rockwork_project" \
    'Rockpool Operation1 watcher in the UI build'
require_fixed 'rockpooloperation.cpp' "$rockwork_project" \
    'Rockpool Operation1 watcher implementation in the UI build'
require_fixed 'connect(m_operation, &RockpoolOperationInterface::Completed' "$rockpool_operation" \
    'Operation1 completion listener installed before snapshot'
require_fixed 'connect(m_properties, &RockpoolOperationPropertiesInterface::PropertiesChanged' \
    "$rockpool_operation" 'Operation1 property listener installed before snapshot'
require_fixed 'm_properties->asyncCall(QStringLiteral("GetAll")' "$rockpool_operation" \
    'authoritative asynchronous Operation1 snapshot'
require_fixed 'epoch != m_snapshotEpoch' "$rockpool_operation" \
    'reordered Operation1 snapshot rejection'
require_fixed 'm_snapshotInFlight' "$rockpool_operation_header" \
    'bounded Operation1 snapshot request state'
require_fixed 'm_snapshotDirty' "$rockpool_operation_header" \
    'coalesced Operation1 snapshot refresh state'
require_fixed 'if (isTerminalState(m_state)) {' "$rockpool_operation" \
    'terminal Operation1 state preserved when canonical readback is no longer available'
require_fixed 'argument.currentSignature() != QStringLiteral("a{sv}")' \
    "$rockpool_operation" 'typed Operation1 result validation'
require_fixed 'QDBusServiceWatcher::WatchForOwnerChange' "$rockpool_operation" \
    'Operation1 owner replacement tracking'
require_fixed 'm_operation->asyncCall(QStringLiteral("Cancel"))' "$rockpool_operation" \
    'asynchronous Operation1 cancellation'
reject_extended 'SIGNAL\(|SLOT\(' "$rockpool_operation_test" \
    'legacy string-based Qt signal syntax in Operation1 regression'
for operation_regression in \
    terminalBeforeSubscribeCompletesFromGetAll \
    terminalSignalBeatsStaleGetAll \
    terminalSignalSurvivesFailedReadback \
    propertiesChangedUpdatesProgressAndTerminalPayload \
    ownerReplacementAndLossInvalidateOldRequests \
    cancelIsAsyncAndIdempotent \
    invalidSnapshotsRetryOnceThenFail \
    signalDuringRetrySupersedesTheTimer
do
    require_fixed "void RockpoolOperationTest::$operation_regression()" \
        "$rockpool_operation_test" "Operation1 regression $operation_regression"
done

# Fire-and-forget legacy watch actions still run from the GUI thread.  They
# must share the watcher-based void-command transport, retain owner identity
# until completion, and never fall back to a synchronous D-Bus call.
require_fixed 'void Pebble::sendVoidCommand(const QString &method, const QVariantList &arguments)' \
    "$rockwork_pebble" 'shared asynchronous compatibility void-command sender'
require_fixed 'm_iface->asyncCallWithArgumentList(method, arguments)' \
    "$rockwork_pebble" 'asynchronous compatibility void-command transport'
require_fixed 'watcher->setProperty("serviceEpoch"' "$rockwork_pebble" \
    'compatibility void-command owner epoch capture'
require_fixed 'void Pebble::voidCommandReplyFinished(QDBusPendingCallWatcher *watcher)' \
    "$rockwork_pebble" 'compatibility void-command completion handler'
require_fixed 'watcher->deleteLater();' "$rockwork_pebble" \
    'compatibility void-command watcher cleanup'
require_fixed 'serviceEpoch != m_serviceEpoch' "$rockwork_pebble" \
    'compatibility void-command stale-owner rejection'
for command in \
    LoadLanguagePack ConfigurationClosed LaunchApp ConfigurationURL RemoveApp \
    InstallApp SideloadApp SetAppOrder RequestScreenshot RemoveScreenshot \
    PerformFirmwareUpgrade
do
    require_fixed "sendVoidCommand(QStringLiteral(\"$command\")" "$rockwork_pebble" \
        "asynchronous compatibility command $command"
done
# Timeline reset is an acknowledgement-only void command.  The window is a
# separate asynchronous, serialized write/readback state machine: it must
# preserve the legacy signed wire tuple and only publish a canonical snapshot.
reject_extended '\.call\(' "$rockwork_pebble" \
    'blocking compatibility watch GUI-thread D-Bus call'
require_fixed 'sendVoidCommand(QStringLiteral("resetTimeline"))' "$rockwork_pebble" \
    'asynchronous compatibility Timeline reset command'
require_fixed 'm_iface->asyncCallWithArgumentList(' "$rockwork_pebble" \
    'asynchronous compatibility Timeline window write transport'
require_fixed 'QStringLiteral("setTimelineWindow")' "$rockwork_pebble" \
    'compatibility Timeline window write method'
require_fixed 'QVariantList() << -start << -fade << end' "$rockwork_pebble" \
    'legacy signed Timeline window write tuple'
for timeline_property in \
    'Q_PROPERTY(int timelineWindowStart READ timelineWindowStart NOTIFY timelineWindowChanged)' \
    'Q_PROPERTY(int timelineWindowFade READ timelineWindowFade NOTIFY timelineWindowChanged)' \
    'Q_PROPERTY(int timelineWindowEnd READ timelineWindowEnd NOTIFY timelineWindowChanged)' \
    'Q_PROPERTY(bool timelineWindowReady READ timelineWindowReady NOTIFY timelineWindowReadyChanged)'
do
    require_fixed "$timeline_property" "$rockwork_pebble_header" \
        'read-only asynchronous compatibility Timeline-window property'
done
for timeline_state in \
    m_timelineWindowRequestEpoch m_timelineWindowWriteEpoch \
    m_timelineWindowWriteInFlight m_timelineWindowWriteQueued \
    m_queuedTimelineWindowWriteEpoch m_inFlightTimelineWindowWriteEpoch \
    m_timelineWindowHasSnapshot m_timelineWindowReadFailures \
    m_pendingTimelineWindowValues m_pendingTimelineWindowReplies
do
    require_fixed "$timeline_state" "$rockwork_pebble_header" \
        "compatibility Timeline-window asynchronous state $timeline_state"
done
require_fixed 'watcher->setProperty("serviceEpoch"' "$rockwork_pebble" \
    'Timeline-window request/write service epoch capture'
require_fixed 'watcher->setProperty("requestEpoch"' "$rockwork_pebble" \
    'Timeline-window snapshot request epoch capture'
require_fixed 'watcher->setProperty("writeEpoch"' "$rockwork_pebble" \
    'Timeline-window write epoch capture'
require_fixed 'if (m_timelineWindowWriteInFlight) {' "$rockwork_pebble" \
    'Timeline-window refresh deferred during write'
require_fixed 'if (m_timelineWindowWriteQueued) {' "$rockwork_pebble" \
    'Timeline-window latest queued write dispatch'
require_fixed 'm_timelineWindowWriteQueued = true;' "$rockwork_pebble" \
    'Timeline-window one queued latest write'
require_fixed 'foreach (const QString &propertyName, timelineWindowProperties())' "$rockwork_pebble" \
    'three-property canonical Timeline-window readback'
require_fixed 'm_pendingTimelineWindowValues.count() != timelineWindowProperties().count()' \
    "$rockwork_pebble" 'complete canonical Timeline-window readback required'
require_fixed 'm_timelineWindowHasSnapshot = false;' "$rockwork_pebble" \
    'service-owner loss clears Timeline-window snapshot fallback'
require_fixed 'void Pebble::timelineWindowWriteReplyFinished(QDBusPendingCallWatcher *watcher)' \
    "$rockwork_pebble" 'Timeline-window write completion handler'
require_fixed 'serviceEpoch != m_serviceEpoch || !m_timelineWindowWriteInFlight' \
    "$rockwork_pebble" 'stale Timeline-window write reply rejection'
require_fixed 'QTimer::singleShot(250, this' "$rockwork_pebble" \
    'bounded Timeline-window snapshot retry'

# The Timeline editor owns drafts in QML.  It refreshes a read-only, canonical
# snapshot, does not overwrite an active edit, and sends exactly one typed
# three-argument request without writing compatibility Q_PROPERTY members.
require_fixed 'root.pebble.refreshTimelineWindow()' "$settings_page" \
    'lazy asynchronous Timeline-window refresh'
require_fixed 'root.pebble.timelineWindowReady' "$settings_page" \
    'Timeline-window readiness gate'
require_fixed 'property bool timelineWindowDirty' "$settings_page" \
    'Timeline-window QML dirty draft state'
require_fixed 'property bool loadingTimelineWindow' "$settings_page" \
    'Timeline-window QML loading guard'
require_fixed 'onTimelineWindowChanged: root.loadTimelineWindow()' "$settings_page" \
    'Timeline-window canonical snapshot loading hook'
require_fixed 'onTimelineWindowReadyChanged: root.loadTimelineWindow()' "$settings_page" \
    'Timeline-window readiness loading hook'
require_fixed 'root.pebble.setTimelineWindow(' "$settings_page" \
    'direct three-argument Timeline-window write request'
require_fixed 'validator: IntValidator { bottom: -365; top: -1 }' "$settings_page" \
    'canonical signed Timeline-window start validator'
require_fixed 'validator: IntValidator { bottom: -2592000; top: 2592000 }' \
    "$settings_page" 'canonical signed Timeline-window fade validator'
require_fixed '&& Number(timelineWindowStartField.text)' "$settings_page" \
    'canonical Timeline-window start/end ordering check'
require_fixed '<= Number(timelineWindowEndField.text)' "$settings_page" \
    'canonical Timeline-window start-before-end comparator'
reject_extended '&&[[:space:]]*-Number\(timelineWindowStartField\.text\)' "$settings_page" \
    'legacy positive-start Timeline-window ordering check'
for timeline_editor_value in \
    'Number(timelineWindowStartField.text)' \
    'Number(timelineWindowFadeField.text)' \
    'Number(timelineWindowEndField.text)'
do
    require_fixed "$timeline_editor_value" "$settings_page" \
        'typed Timeline-window editor argument'
done
reject_extended 'pebble\.timelineWindow(Start|Fade|End)[[:space:]]*=' "$settings_page" \
    'Timeline editor writes read-only compatibility properties directly'

for timeline_regression in \
    timelineActionsDoNotWaitAndWindowReadbackIsCanonical \
    resetTimelineErrorDoesNotChangeTimelineWindow \
    rejectedTimelineWindowWriteRollsBackToCanonicalReadback \
    newerTimelineWindowWriteBeatsOlderReplyAndReadback \
    timelineWindowAcceptsNegativeEnd \
    thirdQueuedTimelineWriteReplacesSecond \
    refreshTimelineWindowWaitsForInFlightWrite \
    ownerReplacementRejectsOldTimelineReadback \
    ownerReplacementCannotRestoreOldTimelineSnapshotAfterReadFailures \
    timelineWindowReadFailuresAreBoundedAndRetainValidatedSnapshot \
    oldOwnerTimelineActionRepliesAreIgnored
do
    require_fixed "void PebbleAsyncTest::$timeline_regression()" \
        "$rockwork_pebble_async_test" \
        "asynchronous Timeline-window regression $timeline_regression"
done
# Account1 is account-global and independently owned by org.rockpool.  The
# Pebbles model owns one client across compatibility-watch destruction; each
# Pebble is only a forwarding facade for existing QML call sites.
require_fixed 'class RockpoolAccount : public QObject' "$rockpool_account_header" \
    'shared primary Account1 client'
require_fixed 'const char ROCKPOOL_SERVICE[] = "org.rockpool";' "$rockpool_account" \
    'independent primary account service name'
require_fixed 'm_interface->asyncCall(QStringLiteral("SetOAuthToken"), token)' \
    "$rockpool_account" 'primary Account1 OAuth operation request'
require_fixed 'QDBusPendingReply<QDBusObjectPath> reply = *watcher;' \
    "$rockpool_account" 'typed Account1 operation-path reply'
require_fixed 'm_tokenEpoch' "$rockpool_account_header" \
    'global OAuth-token command ordering epoch'
require_fixed 'tokenEpoch != m_tokenEpoch' "$rockpool_account" \
    'stale global OAuth-token reply rejection'
require_fixed 'm_tokenOperations.insert(operation);' "$rockpool_account" \
    'retained superseded Account1 operation watcher'
require_fixed 'm_tokenOperations.remove(operation);' "$rockpool_account" \
    'terminal Account1 operation cleanup'
require_fixed '++m_propertiesEpoch;' "$rockpool_account" \
    'OAuth-token authoritative account readback invalidation'
require_fixed 'refreshProperties();' "$rockpool_account" \
    'OAuth-token authoritative account refresh'
require_fixed 'QDBusServiceWatcher *m_serviceWatcher;' "$rockpool_account_header" \
    'independent primary account owner watcher'
require_fixed 'm_ownerEpoch' "$rockpool_account_header" \
    'primary account owner-generation state'
require_fixed 'watcher->setProperty("accountOwnerEpoch"' "$rockpool_account" \
    'primary account reply owner-generation capture'
require_fixed 'ownerEpoch != m_ownerEpoch' "$rockpool_account" \
    'stale primary account reply rejection'
require_fixed 'ownerEpoch == m_ownerEpoch' "$rockpool_account" \
    'stale primary account proxy-signal rejection'
require_fixed 'void RockpoolAccount::serviceOwnerChanged(' "$rockpool_account" \
    'primary account owner-change handler'
require_fixed '++m_ownerEpoch;' "$rockpool_account" \
    'primary account owner-change invalidation'
require_fixed 'resetInterfaces();' "$rockpool_account" \
    'primary account owner-change proxy recreation'
require_fixed 'm_account = new RockpoolAccount(this);' "$rockwork_pebbles" \
    'model-owned global Account1 client'
require_fixed 'new Pebble(p, this, m_account);' "$rockwork_pebbles" \
    'shared Account1 client watch-facade injection'
require_fixed 'RockpoolAccount *m_account;' "$rockwork_pebbles_header" \
    'model-owned Account1 lifetime state'
require_fixed 'RockpoolAccount *m_account;' "$rockwork_pebble_header" \
    'Account1 forwarding facade state'
require_fixed 'm_account->setOAuthToken(token);' "$rockwork_pebble" \
    'Account1 forwarding facade write'
require_fixed 'rockpoolaccount.cpp' "$rockwork_project" \
    'Rockwork Account1 client build source'
require_fixed '../rockpoolaccount.cpp' "$rockwork_pebble_async_project" \
    'Account1 client private-bus test source'
require_fixed '../rockpooloperation.cpp' "$rockwork_pebble_async_project" \
    'Operation1 watcher private-bus test source'
reject_extended 'm_account(TokenEpoch|OwnerEpoch|PropertiesEpoch|TokenOperations)|Rockpool(Account|Properties)Interface' \
    "$rockwork_pebble_header" 'per-watch primary Account1 state'
reject_extended 'm_iface[^;]*([Ss]etOAuthToken|SetOAuthToken)|asyncCall[^;]*SetOAuthToken' \
    "$rockwork_pebble" 'legacy or per-watch OAuth transport'
if ! awk '
    /void RockpoolAccount::serviceOwnerChanged\(/ { in_handler = 1 }
    in_handler && /if \(!newOwner\.isEmpty\(\)\)/ { acquiring = 1 }
    in_handler && acquiring && /refreshProperties\(\);/ { refreshed = 1 }
    in_handler && /^}/ { exit !(acquiring && refreshed) }
    END { exit !(acquiring && refreshed) }
' "$rockpool_account"
then
    fail "primary account owner acquisition does not refresh $rockpool_account"
fi
require_fixed 'property bool oauthBootFlow' "$app_settings_page" \
    'explicit trusted OAuth boot-flow capability'
require_fixed 'property bool isBootFlow: oauthBootFlow && url === "https://boot.rebble.io"' \
    "$app_settings_page" 'exact trusted OAuth boot origin'
require_fixed 'if (appSettings.isBootFlow' "$app_settings_page" \
    'OAuth callback trusted-flow gate'
require_fixed 'action.indexOf("custom-boot-config-url") === 0' \
    "$app_settings_page" 'OAuth callback action validation'
require_fixed 'oauthBootFlow: true' "$settings_page" \
    'trusted login-page OAuth capability grant'
require_fixed 'pebble.accountTokenPending' "$app_settings_page" \
    'OAuth operation pending UI'
require_fixed 'pebble.accountTokenError' "$app_settings_page" \
    'OAuth operation error UI'
require_fixed 'RockpoolAccount::oauthTokenFromCallback(callbackUrl)' \
    "$rockwork_pebble" 'C++ OAuth callback-token parser use'
require_fixed 'validPercentEncoding' "$rockpool_account" \
    'strict OAuth callback percent-encoding validation'
require_fixed 'QUrl::fromPercentEncoding(encoded.toUtf8())' "$rockpool_account" \
    'component-only OAuth token decoding'
require_fixed 'pebble.setOAuthTokenFromCallback(u)' "$app_settings_page" \
    'opaque OAuth callback handoff without QML token retention'
reject_extended 'extractAccessToken|decodeURIComponent' "$app_settings_page" \
    'whole-URL or QML OAuth-token decoding'
reject_extended 'console\.log\([^)]*(url|token|data)|dump\([^)]*(href|url|token)' \
    "$app_settings_page" 'OAuth URL or token logging'
for oauth_parser_case in \
    direct-padding direct-encoded-ampersand direct-encoded-plus \
    direct-encoded-slash outer-encoded-padding \
    outer-encoded-reserved-token malformed-short-percent \
    malformed-nonhex-percent empty-token wrong-scheme wrong-action
do
    require_fixed "QTest::newRow(\"$oauth_parser_case\")" \
        "$rockwork_pebble_async_test" \
        "opaque OAuth callback parser regression $oauth_parser_case"
done
reject_extended 'm_iface->call\("(LoadLanguagePack|ConfigurationClosed|LaunchApp|ConfigurationURL|RemoveApp|InstallApp|SideloadApp|SetAppOrder|RequestScreenshot|RemoveScreenshot|PerformFirmwareUpgrade)"\)' \
    "$rockwork_pebble" 'blocking converted compatibility command'
require_fixed 'enabled: pebble && pebble.connected && languages.length > 0' \
    "$language_page" 'connected-watch language-pack submission gate'
require_fixed 'enabled: pebble && pebble.connected' "$settings_page" \
    'connected-watch Timeline reset gate'
require_fixed 'enabled: root.pebble && root.pebble.connected' "$screenshots_page" \
    'connected-watch screenshot capture gate'
require_fixed 'enabled: root.pebble && root.pebble.connected' \
    "$developer_tools_page" 'connected-watch developer-connection gate'
require_fixed 'if (root.pebble && root.pebble.connected' \
    "$developer_tools_page" 'developer-connection click guard'
require_fixed 'text: qsTr("Send watch logs")' "$developer_tools_page" \
    'watch-log action label'
reject_extended 'fetchProperty' "$rockwork_pebble" \
    'dead blocking compatibility property fetch helper'
require_fixed 'connect(pebble, &Pebble::identityChanged' "$rockwork_pebbles" \
    'late watch identity model update'
require_fixed 'void Pebbles::pebbleIdentityChanged()' "$rockwork_pebbles" \
    'late watch identity sorting and role notification'
require_fixed 'const int row = m_pebbles.indexOf(pebble);' "$rockwork_pebbles" \
    'pointer-safe watch row lookup before asynchronous identity arrives'
reject_extended 'm_iface->call\("InstalledApps"\)|fetchVarMap\("NotificationsFilter"\)|fetchProperty\("Screenshots"\)|fetchProperty\("FirmwareUpgradeAvailable"\)' \
    "$rockwork_pebble" 'blocking compatibility-watch bootstrap snapshot'
reject_extended 'm_iface->call\("(WeatherLocations|SetWeatherLocations|setWeatherUnits|setWeatherLanguage|setWeatherAltKey)"\)|fetchProperty\("Weather(Units|Language|AltKey)"\)' \
    "$rockwork_pebble" 'blocking compatibility weather setting call'
reject_extended 'm_iface->call\("(DevConnectionEnabled|DevConnectionState|getLogLevel|SetDevConnEnabled|setLogLevel|DumpLogs)"\)|fetchProperty\("(DevConnectionEnabled|DevConnectionState|getLogLevel)"\)' \
    "$rockwork_pebble" 'blocking compatibility developer-tools call'
reject_extended 'fetchVariantList|m_iface->call\("Timeline(Colors|Icons)"\)' \
    "$rockwork_pebble" 'blocking compatibility notification-palette call'
reject_extended 'm_iface->call\("(SetNotificationFilter|ForgetNotificationFilter)"\)' \
    "$rockwork_pebble" 'blocking compatibility notification-filter command'
reject_extended 'm_iface->call\("(SetImperialUnits|SetProfileWhenConnected|SetProfileWhenDisconnected|SetCalendarSyncEnabled|setSyncAppsFromCloud)"\)|fetchProperty\("(ImperialUnits|ProfileWhenConnected|ProfileWhenDisconnected|CalendarSyncEnabled|syncAppsFromCloud)"\)' \
    "$rockwork_pebble" 'blocking compatibility settings call'
reject_extended 'fetchVarMap\("(cannedResponses|getCannedResponses)"|sendVarMap\("setCannedResponses"|m_iface->call\("setCannedResponses"' \
    "$rockwork_pebble" 'blocking compatibility canned-response call'
reject_extended 'fetchVarMap\("getFavoriteContacts"|sendVarMap\("setFavoriteContacts"|m_iface->call\("(getFavoriteContacts|setFavoriteContacts)"' \
    "$rockwork_pebble" 'blocking compatibility favorite-contact call'
reject_extended 'QDBusConnection::sessionBus\(\)\.connect' "$rockwork_pebble" \
    'string-based compatibility-watch D-Bus signal hook'
for regression in \
    constructorQueuesHeldBootstrapCalls newestAppsReplyWins \
    newestNotificationReplyWins newestScreenshotReplyWins \
    newestFirmwareReplyWins oldOwnerAppReplyIsIgnored \
    compatibilityCommandsDoNotWaitAndPreserveArguments \
    compatibilityCommandErrorsDoNotSynthesizeState \
    oldOwnerCompatibilityCommandReplyIsIgnored \
    oauthTokenCallbackParsing \
    oauthUsesPrimaryOperationAndRefreshesOnlyAtTerminal \
    oauthOperationFailureIsExposed \
    olderOAuthOperationIsRetainedButCannotFinishLatestWrite \
    staleOAuthMethodReplyIsIgnored \
    accountOwnerReplacementRejectsOldOAuthWork \
    oauthTransportErrorIsExposed \
    pebbleFacadesShareLatestOAuthOperationState \
    sharedOAuthOperationSurvivesPebbleReplacement \
    accountOwnerAcquisitionRefreshesCachedValues \
    oldAccountOwnerReplyIsIgnored accountOwnerLossClearsCachedValues \
    weatherSettingsLoadLazilyAndAtomically newestWeatherSettingsReplyWins \
    oldOwnerWeatherSettingsReplyIsIgnored weatherSettersDoNotWaitForReplies \
    failedWeatherSetterRefreshesAndRollsBack \
    developerSettingsLoadLazilyAndAtomically newestDeveloperSettingsReplyWins \
    developerConnectionSignalRefreshesCurrentValues \
    oldOwnerDeveloperSettingsReplyIsIgnored developerSettersDoNotWaitForReplies \
    failedDeveloperSettersRefreshAndRollBack \
    dumpLogsDoesNotWaitForReplyAndReportsError \
    dumpLogsOwnerChangeFailsOnceAndIgnoresOldCompletion \
    timelinePalettesLoadLazilyAndIndependently \
    failedTimelinePaletteFetchesCanRetry \
    oldOwnerTimelinePaletteRepliesAreIgnored \
    notificationFilterCommandsDoNotWaitForReplies \
    staleNotificationFilterCommandErrorIsIgnored \
    oldOwnerNotificationFilterCommandErrorIsIgnored \
    settingsPageLoadsLazilyAndAtomically newestSettingsPageReplyWins \
    settingsPageSignalsRefreshNewestValues oldOwnerSettingsPageReplyIsIgnored \
    settingsPageSettersDoNotWaitAndRemainIndependent \
    failedSettingsPageSettersRollBackAndReadBack \
    staleSettingsPageSetterRepliesAreIgnored \
    settingsSignalMakesCachedValueWritable \
    failedSettingsReadFallsBackAndMakesValueWritable \
    healthParamsLoadLazilyAndDecodeDbusVariantMap \
    newestHealthParamsReplyWinsAfterSignal \
    healthParamsWriteDoesNotWaitAndReadsBack \
    staleHealthParamsWriteErrorIsIgnored \
    oldOwnerHealthParamsReplyAndErrorAreIgnored \
    coldHealthParamsReadFailureRemainsNotReady \
    retainedHealthParamsFallbackRemainsWritable \
    healthParamsAndImperialUnitsRemainIndependent \
    cannedResponsesLoadLazilyAndUseCachedGetter \
    newestCannedResponsesReplyWins \
    oldOwnerCannedResponsesReplyIsIgnored \
    cannedResponsesWritesMergeClearAndReadBack \
    failedCannedResponsesWriteRollsBackAndReadsBack \
    staleCannedResponsesWriteErrorIsIgnored \
    failedCannedResponsesReadFallsBackAndRemainsWritable \
    cannedContactsLoadLazilyAndUseCachedGetter \
    newestCannedContactsReplyWins \
    oldOwnerCannedContactsReplyIsIgnored \
    cannedContactsWritesReplaceClearAndReadBack \
    failedCannedContactsWriteRollsBackAndReadsBack \
    staleCannedContactsWriteErrorIsIgnored \
    failedCannedContactsReadFallsBackAndRemainsWritable
do
    require_fixed "void PebbleAsyncTest::$regression()" "$rockwork_pebble_async_test" \
        "asynchronous compatibility-watch regression $regression"
done

# libpebble3's application locker is account-global, so the daemon rejects
# global app mutations unless exactly one watch is known. Keep app browsing,
# launch, and configuration available while preventing the rejected actions
# from being offered by the compatibility UI.
for guarded_app_page in \
    "$installed_apps_page" \
    "$app_upgrade_page" \
    "$app_store_details_page" \
    "$import_package_page"
do
    require_fixed 'property bool appMutationsAllowed: rockPool.knownPebbleCount === 1' \
        "$guarded_app_page" 'single-watch application-mutation guard'
    require_fixed 'App changes are available only when exactly one watch is paired.' \
        "$guarded_app_page" 'single-watch application-mutation explanation'
done
require_fixed 'appMutationsAllowed: root.appMutationsAllowed' "$installed_apps_page" \
    'installed-app delegate mutation guard propagation'
delegate_mutation_guards=$(awk '
    index($0, "enabled: root.appMutationsAllowed") { count++ }
    END { print count + 0 }
' "$installed_app_delegate")
if [ "$delegate_mutation_guards" -lt 3 ]; then
    fail "installed-app delete and move controls are not single-watch guarded in $installed_app_delegate"
fi
require_fixed 'enabled: root.appMutationsAllowed' "$installed_apps_page" \
    'application-order commit guard'
require_fixed 'property bool appInstallationAllowed: appMutationsAllowed && pebble && pebble.connected' \
    "$app_upgrade_page" 'connected-watch application-upgrade gate'
require_fixed 'enabled: root.appInstallationAllowed && root.app && !installing && !root.app.companion' \
    "$app_upgrade_page" 'application-upgrade guard'
require_fixed 'property bool appInstallationAllowed: appMutationsAllowed && pebble && pebble.connected' \
    "$app_store_details_page" 'connected-watch application-install gate'
require_fixed 'enabled: root.appInstallationAllowed && !installed && !installing && !root.app.companion' \
    "$app_store_details_page" 'application-install guard'
require_fixed 'property bool appInstallationAllowed: appMutationsAllowed && pebble && pebble.connected' \
    "$import_package_page" 'connected-watch package sideload gate'
require_fixed 'enabled: model.isDir || root.appInstallationAllowed' "$import_package_page" \
    'package sideload guard preserving directory browsing'
require_fixed 'if (root.pebble && root.pebble.connected)' "$installed_apps_page" \
    'connected-watch app launch handler'
require_fixed 'pebble.launchApp(model.uuid)' "$installed_apps_page" \
    'watch-addressed app launch control'
require_fixed 'onConfigureApp: root.configureApp(model.uuid)' "$installed_apps_page" \
    'watch-addressed app configuration control'
require_fixed 'enabled: root.watchConnected' "$installed_app_delegate" \
    'connected-watch app launch gate'
require_fixed 'enabled: root.watchConnected || root.offlineSettingsAvailable' \
    "$installed_app_delegate" 'connected-watch PKJS configuration gate'
require_fixed 'if (!appSettings.oauthBootFlow && !appSettings.pebble.connected)' \
    "$app_settings_page" 'disconnected PKJS configuration retirement'

# A timed-out or abandoned pairing must clear libpebble3's persistent connect
# goal before scanning resumes; otherwise it can keep retrying off-page and
# pair unexpectedly later. Both the timeout and page-destruction paths do it.
# A newly inserted watch has no identity yet. PairWatchPage only clears its
# pending address when Pebbles subsequently emits that exact address, and the
# root reload remains deferred so the page-local handler runs first.
require_fixed 'id: stackReloadTimer' "$rockpool_qml" \
    'deferred compatibility stack transition timer'
require_fixed 'onTriggered: rockPool.loadStack()' "$rockpool_qml" \
    'deferred compatibility stack transition'
require_fixed 'void pebbleIdentityAvailable(const QString &address);' "$rockwork_pebbles_header" \
    'address-bearing compatibility watch identity signal'
require_fixed 'emit pebbleIdentityAvailable(pebble->address());' "$rockwork_pebbles" \
    'nonempty asynchronous compatibility watch identity publication'
require_fixed 'if (!pebble->address().isEmpty()' "$rockwork_pebbles" \
    'empty compatibility watch identity suppression'
require_fixed '!m_pebblesWithIdentity.contains(pebble)' "$rockwork_pebbles" \
    'one-shot compatibility watch identity publication'
require_fixed 'void Pebble::scheduleAddressRetry(quint64 requestEpoch, quint64 serviceEpoch)' \
    "$rockwork_pebble" 'bounded compatibility watch address retry'
require_fixed 'QTimer::singleShot(delayMs, this' "$rockwork_pebble" \
    'nonblocking compatibility watch address retry timer'
require_fixed 'requestEpoch == m_propertyEpochs.value(QStringLiteral("Address"))' \
    "$rockwork_pebble" 'current-request compatibility watch address retry gate'
require_fixed 'value.type() != QVariant::String' "$rockwork_pebble" \
    'malformed compatibility watch address rejection'
require_fixed 'void PebbleAsyncTest::addressBootstrapRetriesAfterInvalidReplies()' \
    "$rockwork_pebble_async_test" 'invalid compatibility watch address retry regression'
require_fixed 'if (pebbles.count < rockPool.knownPebbleCount)' "$rockpool_qml" \
    'watch removal stack reload'
require_fixed 'if (rockPool.waitingForPebbleIdentity' "$rockpool_qml" \
    'identity-gated deferred stack reload'
require_fixed 'property string pendingPairAddress: ""' "$rockpool_qml" \
    'application-level pending pair identity'
require_fixed 'address.toLowerCase()' "$rockpool_qml" \
    'case-insensitive root pair identity match'
require_fixed 'rockPool.pendingPairAddress.toLowerCase()' "$rockpool_qml" \
    'target-specific root pair identity gate'
require_fixed 'if (!pebbles.connectedToService || pebbles.count === 0)' "$rockpool_qml" \
    'service loss and empty watch-list stack reload'
reject_extended 'onConnectedToServiceChanged:[[:space:]]*stackReloadTimer\.restart\(' \
    "$rockpool_qml" 'stack reload on service acquisition before watch identity'
reject_extended 'onCountChanged:[[:space:]]*stackReloadTimer\.restart\(' "$rockpool_qml" \
    'stack reload on watch insertion before identity'
require_fixed 'onPebbleIdentityAvailable:' "$pair_watch_page" \
    'pair-page identity-specific completion handler'
require_fixed 'pairPage.connectingTo.toLowerCase()' "$pair_watch_page" \
    'pair-page exact pending-address completion'
require_fixed 'rockPool.pendingPairAddress = modelData.address' "$pair_watch_page" \
    'pair-page publishes pending target identity'
require_fixed 'rockPool.pendingPairAddress = ""' "$pair_watch_page" \
    'pair-page retires pending target identity'
reject_extended 'onCountChanged:[[:space:]]*pairPage\.connectingTo[[:space:]]*=' "$pair_watch_page" \
    'count-based pair completion'
reject_extended 'onCountChanged:[[:space:]]*loadStack\(' "$rockpool_qml" \
    'synchronous pair-page destruction on successful model insertion'
pair_disconnects=$(awk '
    index($0, "pebbles.disconnectWatch(pairPage.connectingTo)") { count++ }
    END { print count + 0 }
' "$pair_watch_page")
if [ "$pair_disconnects" -lt 2 ]; then
    fail "pairing timeout/destruction does not disconnect the pending watch in $pair_watch_page"
fi

# The Sailfish i18n feature snapshots TRANSLATIONS when it is loaded.  Keep
# the declarations ahead of it, and keep RPM packaging to copying and
# compiling the checked-in catalogs rather than updating their source TS files.
if ! awk '
    /^[[:space:]]*TRANSLATIONS[[:space:]]*\+=[[:space:]]*\$\$files\(translations\/\*\.ts,true\)/ {
        translations = NR
    }
    /^[[:space:]]*CONFIG[[:space:]]*\+=[[:space:]]*sailfishapp_i18n([[:space:]]|$)/ {
        i18n = NR
    }
    /^[[:space:]]*load\(sailfishapp_i18n\)[[:space:]]*$/ {
        load_i18n = NR
    }
    load_i18n && /^[[:space:]]*qm\.commands[[:space:]]*=/ {
        in_qm_commands = 1
        qm_override = 1
    }
    load_i18n && /^[[:space:]]*qm\.commands[[:space:]]*\+=/ {
        in_qm_commands = 1
    }
    in_qm_commands {
        if ($0 ~ /cp -af[[:space:]]+\$\$\{TRANSLATIONS_IN\}/) {
            copies_translations = 1
        }
        if ($0 ~ /lrelease[[:space:]]+\$\$\{TRANSLATE_UNFINISHED\}[[:space:]]+\$\$\{TRANSLATIONS_OUT\}/) {
            releases_translations = 1
        }
        if ($0 ~ /lupdate/) {
            updates_translations = 1
        }
        if ($0 !~ /\\[[:space:]]*$/) {
            in_qm_commands = 0
        }
    }
    END {
        exit !(translations && i18n && load_i18n && translations < i18n &&
            i18n < load_i18n && qm_override && copies_translations &&
            releases_translations && !updates_translations)
    }
' "$rockwork_project"
then
    fail "Rockwork translation declaration or RPM qmake rule is unsafe in $rockwork_project"
fi

# Token material must never be observable through this public API.  Setters
# are allowed, but Get*Token methods and readable token properties are not.
reject_extended '<method[[:space:]][^>]*name="Get[^"]*[Tt]oken"' "$xml" \
    'forbidden OAuth-token getter'

if awk '
    BEGIN { RS = ">"; forbidden = 0 }
    /<property[[:space:]]/ &&
        /name="[^"]*[Tt]oken[^"]*"/ &&
        /access="(read|readwrite)"/ {
            forbidden = 1
            exit
        }
    END { exit forbidden ? 0 : 1 }
' "$xml"
then
    fail "forbidden readable token property in $xml"
fi

# Dump always targets the fixed Downloads location, so it must not accept an
# input path (or an equivalent caller-selected destination) argument.
if awk '
    BEGIN { RS = ">"; in_dump = 0; forbidden = 0 }
    /<method[[:space:]][^>]*name="Dump"/ { in_dump = 1 }
    in_dump && /<arg[[:space:]]/ &&
        /name="[^"]*([Pp]ath|[Ff]ile|[Dd]estination|[Dd]irectory)[^"]*"/ {
            forbidden = 1
            exit
        }
    /<\/method[[:space:]]*$/ { in_dump = 0 }
    END { exit forbidden ? 0 : 1 }
' "$xml"
then
    fail "forbidden caller-selected log path argument in Dump method of $xml"
fi

printf '%s: contract artifacts are valid\n' "$program"
