# Emit a C string from GLSL. Meson supplies -v name=<symbol> and captures stdout.
BEGIN { printf "/* Generated from GLSL; do not edit. */\nstatic const char %s[] =\n", name }
{
    printf "    \"";
    for (i = 1; i <= length($0); i++) {
        character = substr($0, i, 1);
        if (character == "\\" || character == "\"")
            printf "\\";
        printf "%s", character;
    }
    print "\\n\"";
}
END { print ";" }
