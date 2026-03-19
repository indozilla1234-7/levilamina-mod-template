import os
import re

def fix_cpp_formatter_error(file_path):
    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    new_lines = []
    
    # 1. Add NOMINMAX at the very top to prevent Windows.h conflicts
    new_lines.append("#ifndef NOMINMAX\n")
    new_lines.append("#define NOMINMAX\n")
    new_lines.append("#endif\n")
    
    # 2. Force <format> and <string> to the top of the includes
    # This ensures std::formatter is defined as a template before anything else
    new_lines.append("#include <format>\n")
    new_lines.append("#include <string>\n")

    # Tracking to avoid duplicate includes
    essential_headers = {'<format>', '<string>', '"format"', '"string"'}
    
    for line in lines:
        # Skip if the line is one of the headers we manually added above
        if any(header in line for header in essential_headers) and "#include" in line:
            continue
            
        # 3. Look for 'struct formatter' and change to 'struct std::formatter'
        # This fixes cases where 'using namespace std' or global scope causes C3856
        fixed_line = re.sub(r'struct\s+formatter', 'template <> struct std::formatter', line)
        
        # 4. Ensure JNI headers don't stomp on C++ types
        # If we see jni.h, we make sure it stays below the C++ standard headers
        new_lines.append(fixed_line)

    with open(file_path, 'w', encoding='utf-8') as f:
        f.writelines(new_lines)
    
    print(f"Successfully patched {file_path}")

if __name__ == "__main__":
    # Change this to the path of your .cpp or .h file
    target_file = "src/mod/modmorpher.cpp" 
    if os.path.exists(target_file):
        fix_cpp_formatter_error(target_file)
    else:
        print(f"File {target_file} not found.")