import re
import sys
import argparse
import os

# Configuration constant for comment column alignment
COMMENT_COLUMN = 80

def remove_comments(content):
    # Remove multi-line comments
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    # Remove single-line comments  
    content = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
    return content

def safe_eval_expression(expr, symbol_table=None):
    """
    Safely evaluate C expressions to get their actual values
    """
    if not expr or not isinstance(expr, str):
        return expr
    
    if symbol_table is None:
        symbol_table = {}
    
    expr = expr.strip()
    
    # Handle simple cases first
    if re.match(r'^[0-9]+$', expr):
        return int(expr)
    elif re.match(r'^0[xX][0-9a-fA-F]+$', expr):
        try:
            return int(expr, 16)
        except:
            return expr
    elif re.match(r'^0[0-7]+$', expr):
        try:
            return int(expr, 8)
        except:
            return expr
    
    # Replace symbol references with their values from the symbol table
    if symbol_table:
        temp_expr = expr
        # Sort by length in descending order to avoid partial replacements
        sorted_symbols = sorted(symbol_table.keys(), key=len, reverse=True)
        for symbol in sorted_symbols:
            if symbol in temp_expr and not temp_expr.startswith(symbol + '('):  # Don't replace function calls
                try:
                    symbol_value = symbol_table[symbol]
                    if isinstance(symbol_value, int):
                        temp_expr = re.sub(r'\b' + re.escape(symbol) + r'\b', str(symbol_value), temp_expr)
                    else:
                        temp_expr = re.sub(r'\b' + re.escape(symbol) + r'\b', str(symbol_value), temp_expr)
                except:
                    pass
        expr = temp_expr
    
    # For more complex expressions, try to evaluate safely
    try:
        # Replace common C operators with Python equivalents
        eval_expr = expr.replace('<<', ' << ')
        eval_expr = eval_expr.replace('>>', ' >> ')
        eval_expr = eval_expr.replace('&', ' & ')
        eval_expr = eval_expr.replace('|', ' | ')
        eval_expr = eval_expr.replace('^', ' ^ ')
        
        # Try to evaluate
        result = eval(eval_expr)
        return int(result) if isinstance(result, (int, float)) else result
    except:
        # If evaluation fails, return original expression
        return expr

def process_file(filename, symbol_table):
    """Process a single C header file and update the symbol table"""
    with open(filename, 'r') as f:
        content = f.read()
    
    clean_content = remove_comments(content)
    
    # Simple line-by-line processing - much more reliable
    lines = clean_content.split('\n')
    defines = []
    
    # Collect all defines from this file
    for line in lines:
        line = line.strip()
        if line.startswith('#define '):
            # More robust parsing - find the first space after #define to separate name from value
            # Handle multiple spaces and complex formatting
            define_match = re.match(r'#define\s+(\w+)\s+(.*)', line)
            if define_match:
                define_name = define_match.group(1)
                original_value = define_match.group(2).strip()
                
                # Only include defines that have actual values (not just preprocessor directives)
                if original_value and not original_value.startswith('#'):
                    # Remove trailing comments from the value
                    value_without_comments = re.sub(r'\s*//.*$', '', original_value)
                    value_without_comments = value_without_comments.strip()
                    
                    if value_without_comments:
                        defines.append((define_name, value_without_comments))
    
    # Evaluate each define with the current symbol table
    matches = []
    for name, original_value in defines:
        # Try to evaluate the value
        evaluated_value = safe_eval_expression(original_value, symbol_table)
        
        # Add to symbol table for future references (only if it's a valid integer)
        if isinstance(evaluated_value, (int, float)):
            symbol_table[name] = int(evaluated_value)
        else:
            symbol_table[name] = evaluated_value
            
        matches.append((name, original_value, evaluated_value))
    
    return filename, matches

def main():
    parser = argparse.ArgumentParser(description='Convert C header files to Python constants with value evaluation')
    parser.add_argument('input_files', nargs='+', help='Input C header files to process')
    parser.add_argument('-o', '--output', help='Output Python file (optional)')
    parser.add_argument('-c', '--column', type=int, default=80, help='Column where comments start (default: 80)')
    parser.add_argument('--class-name', default='Logsyms', help='Class name for the generated constants (default: Logsyms)')
    parser.add_argument('--module-level', action='store_true', help='Generate module-level constants instead of class-level')
    
    args = parser.parse_args()
    
    # Validate input files
    for input_file in args.input_files:
        if not os.path.exists(input_file):
            print(f"Error: File '{input_file}' does not exist")
            sys.exit(1)
    
    # Single symbol table across all files
    global_symbol_table = {}
    
    # Process all files in command line order
    all_matches = []
    
    for filename in args.input_files:
        try:
            file_name, matches = process_file(filename, global_symbol_table)
            if matches:  # Only include files that have defines
                all_matches.extend(matches)
        except Exception as e:
            print(f"Error processing {filename}: {e}")
            sys.exit(1)
    
    # Generate the output with all constants
    if all_matches:
        output_lines = []
        
        # Default to module-level unless --class-name is explicitly specified
        use_class_level = bool(args.class_name)
        if not args.module_level and not use_class_level:
            args.module_level = True  # Default to module-level
        
        output_lines.append("### Do Not Edit - Automatically Generated via:")
        output_lines.append("###")
        cmd_line = " ".join(sys.argv)
        output_lines.append("###     " + cmd_line)
        output_lines.append("###")
        output_lines.append('')
        
        if args.module_level:
            # Generate module-level constants
            output_lines.append("# Module-level constants extracted from C header files")
            output_lines.append("")
            
            # Process all matches and generate formatted output
            for name, original_value, evaluated_value in all_matches:
                # Format the evaluated value properly
                if isinstance(evaluated_value, int):
                    formatted_value = hex(evaluated_value) if evaluated_value > 0 else str(evaluated_value)
                else:
                    formatted_value = str(evaluated_value)
                
                # Calculate spaces needed for proper column alignment
                # Account for " = " and the original value length when calculating padding
                total_length = len(name) + 3 + len(original_value)  # name + " = " + original_value
                spaces_needed = max(0, args.column - total_length - 1)  # args.column minus total length minus " #"
                formatted_line = f"{name} = {original_value}{' ' * spaces_needed}# {formatted_value}"
                output_lines.append(formatted_line)
        else:
            # Generate class-level constants (old behavior)
            output_lines.append(f"class {args.class_name}:")
            output_lines.append("# Class containing constants extracted from the specified C header files")
            output_lines.append("")
            
            # Process all matches and generate formatted output
            for name, original_value, evaluated_value in all_matches:
                # Format the evaluated value properly
                if isinstance(evaluated_value, int):
                    formatted_value = hex(evaluated_value) if evaluated_value > 0 else str(evaluated_value)
                else:
                    formatted_value = str(evaluated_value)
                
                # Calculate spaces needed for proper column alignment
                # Account for " = " and the original value length when calculating padding
                total_length = len(name) + 3 + len(original_value)  # name + " = " + original_value
                spaces_needed = max(0, args.column - total_length - 1)  # args.column minus total length minus " #"
                formatted_line = f"    {name} = {original_value}{' ' * spaces_needed}# {formatted_value}"
                output_lines.append(formatted_line)
        
        full_output = '\n'.join(output_lines)

        # Make sure that the output always terminates with a newline:
        if full_output and not full_output.endswith('\n'):
            full_output += '\n'

        if args.output:
            # Write to the specified output file
            with open(args.output, 'w') as f:
                f.write(full_output)
            print(f"Successfully converted {len(args.input_files)} files to {args.output}")
        else:
            # write to stdout
            print(full_output)
    else:
        print("No defines found in any input files")

if __name__ == "__main__":
    main()
