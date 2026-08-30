file(GLOB GUI_SRC_LIST
        *.cpp
        *.c
        *.h
)

# Standalone checks carry their own main(); they are built by hand from the
# command in their header comment, not linked into the app.
list(FILTER GUI_SRC_LIST EXCLUDE REGEX "/test_[^/]*$")

target_sources(${PROJECT_NAME} PRIVATE ${GUI_SRC_LIST})
