# SrvWrap - Wrap a console program as a Windows service

This is a replacement for the old SrvAny.exe, equally simple but much more polished.

- Wraps any console program as a Windows service
- No changes to program code
- Wraps Java programs such as web service providers
- No changes to Java code
- Extremely lightweight - one 70K executable
- Extremely simple to configure
- The wrapped service is managed using native Windows service management tools
- Installs, starts, stops, uninstalls exactly like a native Windows service
- Reports status exactly like a native Windows service
- Starting the service starts program execution
- If the program terminates itself, the service moves from Running status to Stopped status
- Stopping the service cleanly terminates program execution

See src/SrvWrap.c for usage.
