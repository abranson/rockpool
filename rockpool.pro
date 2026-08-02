TEMPLATE = subdirs
# The retired C++ daemon is gone; this release builds only the temporary
# compatibility UI while its ObjectManager migration is completed.
SUBDIRS = rockwork
OTHER_FILES += \
    README.md \
    rpm/rockpool.spec \
    rpm/rockpool.yaml \
    rpm/rockpool.changes
