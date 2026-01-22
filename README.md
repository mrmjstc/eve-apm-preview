<img width="985" height="832" alt="PaeWES9OJ0" src="https://github.com/user-attachments/assets/f7a64339-f57b-4296-931e-43af7885e117" />

# EVE-APM Preview

## What is EVE-APM Preview?

EVE-APM Preview creates small thumbnail previews of all your EVE Online client windows, making it easy to monitor and switch between multiple characters at a glance using clicks or hotkeys. Heavily inspired by the original EVE-O Preview and related tools, with fresh new features and configuration options.

## What EVE-APM Preview Isn't

It's **never** going to let you input broadcast, display portions of your eve client, manipulate your eve client in any way or intentionally help you break the EVE Online EULA/TOS. 

## Why not just use EVE-O/X Preview?

Choice! You should use whatever you prefer and suits your needs, I designed this around ease-of-setup. No need to edit configuration files when you can do it all from a configuration interface!

## Key Features

### Window Management
- **Live Thumbnails** - See real-time previews of all your EVE Online windows in one place
- **Quick Switching** - Click any thumbnail to instantly bring that window to the front
- **Always Visible** - Thumbnails stay on top so you can monitor all your clients while playing
- **Automatic Detection** - Automatically finds and tracks your EVE Online windows
- **Auto-Minimize Inactive Clients** - Automatically minimize EVE clients when you're not using them (with customizable delay)
- **Active Window Highlighting** - Visual highlight border around the currently active client (customizable color and width)
- **Hide Active Thumbnail** - Option to hide the thumbnail of the client you're currently playing
- **Customizable Thumbnail Size** - Adjust thumbnail dimensions to fit your screen layout
- **Adjustable Opacity** - Set transparency level for thumbnails to balance visibility with screen space

### Hotkeys & Switching
- **Global Hotkey Support** - Set up custom keyboard shortcuts to switch between windows instantly
- **Profile Switching** - Create multiple configuration profiles and switch between them with hotkeys
- **EVE-Focused Mode** - Optionally restrict hotkeys to only work when EVE windows have focus

### Protocol Handler (eveapm://)
- **Profile Switching** - Switch to different profiles using `eveapm://profile/<name>`
- **Character Activation** - Bring specific characters to the front using `eveapm://character/<name>`
- **Hotkey Management** - Suspend or resume hotkeys remotely with `eveapm://hotkey/suspend` or `eveapm://hotkey/resume`
- **Thumbnail Control** - Hide or show thumbnails using `eveapm://thumbnail/hide` or `eveapm://thumbnail/show`
- **Settings Access** - Open the configuration dialog with `eveapm://config/open`

### Position & Layout
- **Remember Positions** - Thumbnails return to their saved positions when characters log in
- **Preserve Logout Positions** - Keep thumbnail positions even after characters log out
- **Lock Positions** - Prevent accidental thumbnail movement once you've arranged them
- **Never-Minimize List** - Designate specific characters that should never be auto-minimized

### Visual Customization
- **Character Names** - Display character names on thumbnails with customizable color, font, and position
- **System Names** - Show current solar system on thumbnails for quick situational awareness
- **Per-Character Thumbnail Border Colors** - Assign unique border colors to specific characters for instant recognition
- **Not Logged In Indicators** - Visual overlays for clients that aren't logged into a character yet
- **Active Thumbnail Border** - 

### Log Monitoring & Alerts
- **Chat and Game Log Monitoring** - Monitor EVE logs for system jumps 
- **Combat Alerts** - Get notified about combat-related events on inactive clients (fleet invites, follow/warp commands, regroups, compression cycles)

## Getting Started

1. Download and run the application
2. Launch your EVE Online clients
3. The application will automatically detect and display thumbnails for each client
4. Click on any thumbnail to switch to that window
5. (Optional) Configure hotkeys in the settings to switch windows even faster

## Requirements

- Windows operating system
- EVE Online installed and running

## Tips

- Arrange the thumbnails to match your screen layout for easier navigation
- Use hotkeys when you need to react quickly without moving your mouse
- The thumbnails update in real-time, so you can monitor all your clients for activity
- Use the "Fixed Window" display setting in your EVE clients for smoother switching of clients

## License

See [LICENSE](LICENSE) for details.

## Credits

This is a C++ Qt reimplementation, and heavily inspired by the original EVE-O Preview and EVE-X Preview tools for EVE Online players. It wouldn't exist if not for the excellent work of those developers that came before me.

---

*EVE Online and the EVE logo are the registered trademarks of CCP hf. All rights are reserved worldwide. All other trademarks are the property of their respective owners. This application is not affiliated with or endorsed by CCP hf.*

