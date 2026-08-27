TEMPLATE = subdirs
# The daemon is built separately; qmake builds only the Rockpool UI.
SUBDIRS = ui
ui.file = ui/rockpool.pro
OTHER_FILES += \
    README.md \
    rpm/rockpool.spec \
    rpm/rockpool.yaml \
    rpm/rockpool.changes
