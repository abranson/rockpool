TEMPLATE = subdirs
SUBDIRS = rockwork rockworkd
OTHER_FILES += \
    README.md \
    rpm/rockpool.spec \
    rpm/rockpool.yaml \
    rpm/rockpool.changes

#  TODO:
# Merge in latest changes from rockwork release
# Migrate VoiceCallManager over to replace callchannelobserver
# Dismiss notifications
# Integrate calendar
# Alarms?
