#ifndef ENUMS_H
#define ENUMS_H

#include <QMetaType>

enum HardwareRevision {
    HardwareRevisionUNKNOWN = 0,
    HardwareRevisionTINTIN_EV1 = 1,
    HardwareRevisionTINTIN_EV2 = 2,
    HardwareRevisionTINTIN_EV2_3 = 3,
    HardwareRevisionTINTIN_EV2_4 = 4,
    HardwareRevisionTINTIN_V1_5 = 5,
    HardwareRevisionBIANCA = 6,
    HardwareRevisionSNOWY_EVT2 = 7,
    HardwareRevisionSNOWY_DVT = 8,
    HardwareRevisionSPALDING_EVT = 9,
    HardwareRevisionBOBBY_SMILES = 10,
    HardwareRevisionSPALDING = 11,
    HardwareRevisionSILK_EVT = 12,
    HardwareRevisionROBERT_EVT = 13,
    HardwareRevisionSILK = 14,

    HardwareRevisionTINTIN_BB = 0xFF,
    HardwareRevisionTINTIN_BB2 = 0xFE,
    HardwareRevisionSNOWY_BB = 0xFD,
    HardwareRevisionSNOWY_BB2 = 0xFC,
    HardwareRevisionSPALDING_BB2 = 0xFB,
    HardwareRevisionSILK_BB = 0xFA,
    HardwareRevisionROBERT_BB = 0xF9,
    HardwareRevisionSILK_BB2 = 0xF8
};

enum OS {
     OSUnknown = 0,
     OSiOS = 1,
     OSAndroid = 2,
     OSOSX = 3,
     OSLinux = 4,
     OSWindows = 5
};

enum HardwarePlatform {
    HardwarePlatformUnknown = 0,
    HardwarePlatformAplite,
    HardwarePlatformBasalt,
    HardwarePlatformChalk,
    HardwarePlatformDiorite,
    HardwarePlatformEmery
};

enum Model {
    ModelUnknown = 0,
    ModelTintinBlack = 1,
    ModelTintinWhite = 2,
    ModelTintinRed = 3,
    ModelTintinOrange = 4,
    ModelTintinGrey = 5,
    ModelBiancaSilver = 6,
    ModelBiancaBlack = 7,
    ModelTintinBlue = 8,
    ModelTintinGreen = 9,
    ModelTintinPink = 10,
    ModelSnowyWhite = 11,
    ModelSnowyBlack = 12,
    ModelSnowyRed = 13,
    ModelBobbySilver = 14,
    ModelBobbyBlack = 15,
    ModelBobbyGold = 16,
    ModelSpalding14Silver = 17,
    ModelSpalding14Black = 18,
    ModelSpalding20Silver = 19,
    ModelSpalding20Black = 20,
    ModelSpalding14RoseGold = 21,
    ModelSilkHrLime = 22,
    ModelSilkHrFlame = 23,
    ModelSilkHrWhite = 24,
    ModelSilkHrAqua = 25,
    ModelSilkSeBlack = 26,
    ModelSilkSeWhite = 27,
    ModelRobertBlack = 28,
    ModelRobertSilver = 29,
    ModelRobertGold = 30
};

enum MusicControlButton {
    MusicControlPlayPause = 0x01,
    MusicControlPause = 0x02,
    MusicControlPlay = 0x03,
    MusicControlNextTrack = 0x04,
    MusicControlPreviousTrack = 0x05,
    MusicControlVolumeUp = 0x06,
    MusicControlVolumeDown = 0x07,
    MusicControlGetCurrentTrack = 0x08,
    MusicControlUpdateCurrentTrack = 0x10,
    MusicControlUpdatePlayStateInfo = 0x11
};

enum CallStatus {
    CallStatusIncoming,
    CallStatusOutGoing
};

enum Capability {
    CapabilityNone                    = 0x0000000000000000,
    CapabilityAppRunState             = 0x0000000000000001,
    CapabilityInfiniteLogDumping      = 0x0000000000000002,
    CapabilityUpdatedMusicProtocol    = 0x0000000000000004,
    CapabilityExtendedNotifications   = 0x0000000000000008,
    CapabilityLanguagePacks           = 0x0000000000000010,
    Capability8kAppMessages           = 0x0000000000000020,
    CapabilityHealth                  = 0x0000000000000040,
    CapabilityVoice                   = 0x0000000000000080,
    CapabilitySendSMS                 = 0x0000000000000100,
    //CapabilityXXX                   = 0x0000000000000200,
    CapabilityUnreadCoreDump          = 0x0000000000000400,
    CapabilityWeather                 = 0x0000000000000800,
    CapabilityReminders               = 0x0000000000001000,
    CapabilityWorkouts                = 0x0000000000002000,
    CapabilitySmoothFwInstallProgress = 0x0000000000004000,
    CapabilityResumableFwInstall      = 0x0000000000200000,
};
Q_DECLARE_FLAGS(Capabilities, Capability)

#endif // ENUMS_H

