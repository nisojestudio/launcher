if(NOT DEFINED INPUT)
    message(FATAL_ERROR "INPUT is required")
endif()

if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()

# Verify the input file is not binary (contains null bytes)
file(READ "${INPUT}" FILE_HEX HEX)
string(REGEX MATCHALL ".." BYTE_PAIRS "${FILE_HEX}")
set(HAS_NULL_BYTE FALSE)
foreach(BYTE_PAIR ${BYTE_PAIRS})
    if(BYTE_PAIR STREQUAL "00")
        set(HAS_NULL_BYTE TRUE)
        break()
    endif()
endforeach()
if(HAS_NULL_BYTE)
    message(WARNING
        "embed_text_asset: input '${INPUT}' contains null bytes; "
        "the embedded string_view will be truncated at the first null byte. "
        "Use a different embedding method for binary files."
    )
endif()

file(READ "${INPUT}" CONTENTS)

# Escape backslashes first so we don't double-escape our own escapes
string(REPLACE "\\" "\\\\" CONTENTS "${CONTENTS}")

# Escape double quotes
string(REPLACE "\"" "\\\"" CONTENTS "${CONTENTS}")

# Normalize Windows line endings to Unix
string(REPLACE "\r\n" "\n" CONTENTS "${CONTENTS}")

# Strip trailing newlines to avoid dangling empty "" literals
string(REGEX REPLACE "\n+$" "" CONTENTS "${CONTENTS}")

# Split at newlines so each line becomes "line\n" on its own line in the .inc file
string(REPLACE "\n" "\\n\"\n\"" CONTENTS "${CONTENTS}")

file(WRITE "${OUTPUT}" "\"${CONTENTS}\"")
