# VSCode Configuration for tbox

This directory contains VSCode configuration files for the tbox C++ project.

## Files

- `settings.json` - Main VSCode settings for C++ development
- `setup_clang.sh` - Helper script to check and configure clang tools

## Clang Configuration

The configuration uses flexible path resolution for clang tools:

### Compiler Priority
1. **Preferred**: `/usr/local/llvm/18/bin/clang` (if available)
2. **Fallback**: System clang from PATH (e.g., `/usr/bin/clang`)

### Setup Instructions

1. **Check current configuration**:
   ```bash
   ./.vscode/setup_clang.sh
   ```

2. **To use preferred clang** (if installed at `/usr/local/llvm/18/bin/`):
   Add to your `~/.bashrc` or `~/.zshrc`:
   ```bash
   export PATH="/usr/local/llvm/18/bin:$PATH"
   ```
   Then restart your terminal or run `source ~/.bashrc`

3. **Verify configuration**:
   ```bash
   which clang
   which clang-format
   ```

## Features

- **IntelliSense**: Configured for C++17 with clang
- **Code Formatting**: Uses clang-format with Google style (fallback)
- **Format on Save**: Automatically formats code when saving
- **Compile Commands**: Uses `compile_commands.json` for accurate IntelliSense
- **Error Squiggles**: Real-time error detection

## Removed Files

- `c_cpp_properties.json` - Removed as redundant (all settings moved to `settings.json`)

## Troubleshooting

If you encounter issues:

1. Run the setup script: `./.vscode/setup_clang.sh`
2. Check that clang is in your PATH: `which clang`
3. Ensure compile_commands.json exists: `ls compile_commands.json`
4. Restart VSCode after PATH changes
