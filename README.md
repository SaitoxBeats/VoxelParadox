# Voxel Paradox

## Build Instructions

### Prerequisites
- Windows
- Visual Studio with C++ desktop development workload installed
- `vcpkg` available on `PATH`, or use the repository's `vcpkg_installed` layout if already prepared

### Steps

1. Open a Developer PowerShell or Command Prompt for Visual Studio.
2. Change directory to the repository root:
   ```powershell
   cd e:\DEV\Cpp\VoxelParadox
   ```
3. Run the bootstrap script to generate MSBuild props and prepare the build layout:
   ```powershell
   .\bootstrap.ps1
   ```
4. Open `VoxelParadox.slnx` in Visual Studio.
5. Select the `x64` platform and your desired configuration (`dev-release`, `Release`, or `Debug`).
6. Build the solution.

### Notes
- Build outputs are written under `artifacts/bin/x64/<Configuration>/<ProjectName>/`.
- `dev-release` is the normal development configuration for day-to-day work.
- If you prefer command-line build, run MSBuild from the repository root:
  ```powershell
  msbuild VoxelParadox.slnx /p:Configuration=dev-release /p:Platform=x64
  ```
