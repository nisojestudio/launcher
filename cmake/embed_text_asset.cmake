if(NOT DEFINED INPUT)
    message(FATAL_ERROR "INPUT is required")
endif()

if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()

file(READ "${INPUT}" CONTENTS)
string(REPLACE "\\" "\\\\" CONTENTS "${CONTENTS}")
string(REPLACE "\"" "\\\"" CONTENTS "${CONTENTS}")
string(REPLACE "\r\n" "\n" CONTENTS "${CONTENTS}")
string(REPLACE "\n" "\\n\"\n\"" CONTENTS "${CONTENTS}")

file(WRITE "${OUTPUT}" "\"${CONTENTS}\"")
