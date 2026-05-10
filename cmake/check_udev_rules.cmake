if(NOT EXISTS /etc/udev/rules.d/60-picotool.rules)
    message(FATAL_ERROR
        "\n"
        "  picotool udev rules are not installed.\n"
        "  Run these commands, then rebuild:\n"
        "\n"
        "    sudo cp ${RULES_SRC} /etc/udev/rules.d/\n"
        "    sudo udevadm control --reload\n"
    )
endif()
