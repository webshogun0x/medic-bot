#pragma once

#include "user_types.h"
#include "system_events.h"
#include <cstdint>

namespace medicbot {

enum class OrchestratorState {
    BOOT = 0,
    STANDBY,
    LOGIN_2FA,
    NEW_USER_ENROLL,
    MEASURING_VITALS,
    DASHBOARD_ACTIVE,
    SAVING_RECORD
};

class KioskOrchestrator {
public:
    KioskOrchestrator();
    ~KioskOrchestrator();

    void begin();
    void processEvent(const sys_event_t &evt);

    OrchestratorState getState() const { return m_state; }
    const user_profile_t &getCurrentUser() const { return m_currentUser; }
    const vital_readings_t &getLatestVitals() const { return m_latestVitals; }

private:
    OrchestratorState m_state;
    user_profile_t m_currentUser;
    vital_readings_t m_latestVitals;

    void handleRfidScanned(const char *uid);
    void handleEnrollmentStep1();
    void handleEnrollmentStep2();
    void handleVerifyFingerprint();
    void executeOrchestratedMeasurement();
    void saveCurrentReadings(int systolic, int diastolic);
    void resetToStandby();

    static void orchestratorTaskTrampoline(void *arg);
    void orchestratorTask();
};

KioskOrchestrator &getOrchestrator();

} // namespace medicbot
