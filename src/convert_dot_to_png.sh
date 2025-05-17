#!/bin/bash

# Default directories
input_dir="output_directory"
output_dir="output_images"
format="png"  # Default output format

# Parse command line arguments
while getopts "i:o:f:h" opt; do
  case $opt in
    i) input_dir="$OPTARG" ;;
    o) output_dir="$OPTARG" ;;
    f) format="$OPTARG" ;;
    h) 
       echo "Usage: $0 [-i input_dir] [-o output_dir] [-f format]"
       echo "  -i: Input directory containing .dot files (default: output_directory)"
       echo "  -o: Output directory for generated images (default: output_images)"
       echo "  -f: Output format (png, svg, pdf, jpg) (default: png)"
       exit 0
       ;;
    \?) echo "Invalid option: -$OPTARG" >&2; exit 1 ;;
  esac
done

# Check if Graphviz is installed
if ! command -v dot &> /dev/null; then
    echo "Error: Graphviz (dot) is not installed. Please install it first."
    echo "  Ubuntu/Debian: sudo apt-get install graphviz"
    echo "  Fedora: sudo dnf install graphviz"
    echo "  macOS: brew install graphviz"
    exit 1
fi

# Check if input directory exists
if [ ! -d "$input_dir" ]; then
    echo "Error: Input directory '$input_dir' doesn't exist."
    exit 1
fi

# Check if there are any dot files
dot_files=("$input_dir"/*.dot)
if [ ! -e "${dot_files[0]}" ]; then
    echo "No .dot files found in '$input_dir'."
    exit 1
fi

# Create output directory if it doesn't exist
mkdir -p "$output_dir"

# Set counters
total=0
success=0
failed=0

echo "Converting .dot files from '$input_dir' to .$format files in '$output_dir'..."

# Process each dot file
for file in "$input_dir"/*.dot; do
    # Skip if not a regular file
    [ -f "$file" ] || continue
    
    ((total++))
    base_name=$(basename "$file" .dot)
    output_file="$output_dir/${base_name}.$format"
    
    echo -n "Processing $base_name... "

    # Try converting the dot file to the desired format
    if dot -T$format "$file" -o "$output_file" 2>/dev/null; then
        echo "✓ Success"
        ((success++))
    else
        echo "✗ Failed"
        ((failed++))
        echo "  Error details:" >&2
        dot -T$format "$file" -o "$output_file" 2>&1 | sed 's/^/  /' >&2
    fi
done

# Print summary
echo ""
echo "Conversion summary:"
echo "  Total: $total files"
echo "  Successful: $success files"
echo "  Failed: $failed files"

if [ $success -gt 0 ]; then
    echo ""
    echo "Output files are in: $output_dir"
fi

exit $failed  # Exit with number of failures as error code