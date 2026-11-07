# BSD/POSIX sed discards the pattern space when N reaches EOF. Avoid relying
# on GNU sed's different behavior when embedding the help text.
file(READ "${INPUT}" text)
# The help command supplies the final newline.
string(REGEX REPLACE "\n$" "" text "${text}")
string(REPLACE "\\" "\\\\" text "${text}")
string(REPLACE "\"" "\\\"" text "${text}")
string(REPLACE "\r" "\\r" text "${text}")
string(REPLACE "\t" "\\t" text "${text}")
string(REPLACE "\n" "\\n" text "${text}")
file(WRITE "${OUTPUT}" "\"${text}\"\n")
