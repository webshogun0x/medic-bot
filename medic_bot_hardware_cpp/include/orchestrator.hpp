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

enum class MeasurementStep {
    IDLE = 0,
    STEP_1_WEIGHT,
    STEP_2_HEIGHT,
    STEP_3_STEPPER_ALIGN,
    STEP_4_TEMP,
    STEP_5_STEPPER_HOME,
    COMPLETE
};

class KioskOrchestrator {
public:
    KioskOrchestrator();
    ~KioskOrchestrator();

    void begin();
    void processEvent(const sys_event_t &evt);

    OrchestratorState getState() const { return m_state; }
    MeasurementStep getMeasurementStep() const { return m_measStep; }
    const user_profile_t &getCurrentUser() const { return m_currentUser; }
    const vital_readings_t &getLatestVitals() const { return m_latestVitals; }

private:
    OrchestratorState m_state;
    MeasurementStep m_measStep;
    uint32_t m_stepStartTimeMs;
    uint8_t m_stepRetryCount;
    float m_targetForeheadHeightCm;
    bool m_stepperHomed;
    bool m_oximeterFinished;
    uint32_t m_oximeterPromptTimeMs;

    user_profile_t m_currentUser;
    vital_readings_t m_latestVitals;

    void handleRfidScanned(const char *uid);
    void handleEnrollmentStep1();
    void handleEnrollmentStep2();
    void handleVerifyFingerprint();
    void executeOrchestratedMeasurement();
    void startMeasurementStep(MeasurementStep step);
    void checkMeasurementTimeouts();
    void finishOrchestratedMeasurement();
    void saveCurrentReadings(int systolic, int diastolic);
    void resetToStandby();

    static void orchestratorTaskTrampoline(void *arg);
    void orchestratorTask();
};

KioskOrchestrator &getOrchestrator();

} // namespace medicbot
