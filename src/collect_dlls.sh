#!/bin/bash

# Help function
show_help() {
    echo "Usage: $0 EXE_PATH DLL_SOURCE_DIR OUTPUT_DIR"
    echo
    echo "Collects required DLLs for a Windows executable."
    echo
    echo "Parameters:"
    echo "  EXE_PATH        Path to the Windows executable"
    echo "  DLL_SOURCE_DIR  Directory containing source DLLs"
    echo "  OUTPUT_DIR      Directory where DLLs will be copied"
    exit 1
}

# Check parameters
if [ $# -ne 3 ]; then
    show_help
fi

EXE_PATH="$1"
DLL_SOURCE_DIR="$2"
OUTPUT_DIR="$3"

# Verify inputs
if [ ! -f "$EXE_PATH" ]; then
    echo "Error: Executable not found: $EXE_PATH"
    exit 1
fi

if [ ! -d "$DLL_SOURCE_DIR" ]; then
    echo "Error: DLL directory not found: $DLL_SOURCE_DIR"
    exit 1
fi

if [ ! -d "$OUTPUT_DIR" ]; then
    echo "Error: Output directory not found: $OUTPUT_DIR"
    exit 1
fi

# Function to get direct dependencies of a file
get_dependencies() {
    local file="$1"
    objdump -p "$file" | grep "DLL Name:" | sed 's/\s*DLL Name: //'
}

# Function to check if a DLL exists in source directory
dll_exists() {
    local dll="$1"
    [ -f "$DLL_SOURCE_DIR/$dll" ]
}

# Initialize arrays for processing
declare -A processed_dlls
declare -a dlls_to_process
declare -a missing_dlls

# Get initial dependencies
echo "Analyzing dependencies for $(basename "$EXE_PATH")..."
initial_dlls=$(get_dependencies "$EXE_PATH")

# Add initial DLLs to processing queue
for dll in $initial_dlls; do
    if dll_exists "$dll"; then
        dlls_to_process+=("$dll")
    else
        missing_dlls+=("$dll")
    fi
done

# Process DLLs recursively
while [ ${#dlls_to_process[@]} -gt 0 ]; do
    current_dll="${dlls_to_process[0]}"
    dlls_to_process=("${dlls_to_process[@]:1}")
    
    # Skip if already processed
    [ "${processed_dlls[$current_dll]}" = "1" ] && continue
    
    echo "Processing: $current_dll"
    
    # Mark as processed
    processed_dlls[$current_dll]=1
    
    # Copy DLL to output directory
    cp "$DLL_SOURCE_DIR/$current_dll" "$OUTPUT_DIR/"
    
    # Get dependencies of current DLL
    subdeps=$(get_dependencies "$DLL_SOURCE_DIR/$current_dll")
    
    # Add new dependencies to processing queue
    for dll in $subdeps; do
        if [ "${processed_dlls[$dll]}" != "1" ]; then
            if dll_exists "$dll"; then
                dlls_to_process+=("$dll")
            else
                if [[ ! " ${missing_dlls[@]} " =~ " ${dll} " ]]; then
                    missing_dlls+=("$dll")
                fi
            fi
        fi
    done
done

# Print summary
echo -e "\nDLL Collection Complete!"
echo "Copied ${#processed_dlls[@]} DLLs"

if [ ${#missing_dlls[@]} -gt 0 ]; then
    echo -e "\nNote: The following DLLs were not found in the source directory:"
    printf '%s\n' "${missing_dlls[@]}"
    echo "These are likely system DLLs that will be present on the target Windows system."
fi
