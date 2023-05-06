#BT
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_QCOM := true

#FM
ifeq ($(TARGET_BOARD_TYPE),auto)
BOARD_HAVE_QCOM_FM := false
else ifeq ($(TARGET_BOARD_AUTO),true)
BOARD_HAVE_QCOM_FM := false
else
BOARD_HAVE_QCOM_FM := true
endif

TARGET_USE_QTI_BT_CONFIGSTORE := true
TARGET_USE_QTI_BT_SAR := true
TARGET_USE_QTI_VND_FWK_DETECT := true
TARGET_USE_BT_DUN := false

ifeq ($(TARGET_USES_QMAA),true)
ifneq ($(TARGET_USES_QMAA_OVERRIDE_BLUETOOTH),true)
BOARD_HAVE_BLUETOOTH_QCOM := false
DISABLE_BT_FTM := true
BOARD_ANT_WIRELESS_DEVICE :=
endif
ifneq ($(TARGET_USES_QMAA_OVERRIDE_FM),true)
BOARD_HAVE_QCOM_FM := false
endif
endif
