#!/usr/bin/env python3
import ast
import operator as op
import math
import os
import sys
import re

class Evaluator:
    def __init__(self):
        self.functions = {
            'pow2': lambda x: 2**x,
            'log2': lambda x: math.log2(x),
            'align_up': lambda v, a: (v + a - 1) & ~(a - 1),
            'max': max,
            'min': min
        }
        
        self.operators = {
            ast.Add: op.add, ast.Sub: op.sub, ast.Mult: op.mul, ast.Div: op.truediv,
            ast.Eq: op.eq, ast.NotEq: op.ne, ast.Gt: op.gt, ast.Lt: op.lt,
            ast.GtE: op.ge, ast.LtE: op.le, ast.LShift: op.lshift, ast.RShift: op.rshift,
            ast.BitAnd: op.and_, ast.BitOr: op.or_, ast.Invert: op.invert,
            ast.And: lambda a, b: a and b, ast.Or: lambda a, b: a or b,
            ast.Not: lambda x: not self._is_truthy(x),
            ast.USub: op.neg,
            ast.In: lambda a, b: a in b,
            ast.NotIn: lambda a, b: a not in b
        }

    def _is_truthy(self, val):
        """Factually converts common C/Env strings to booleans."""
        if isinstance(val, bool): return val
        if isinstance(val, (int, float)): return val != 0
        if isinstance(val, str):
            if val.lower() in ('false', '0', 'no', 'off', ''): return False
            return True
        return bool(val)

    def _eval(self, node):
        if isinstance(node, ast.Constant):
            return node.value
        
        elif isinstance(node, ast.Name):
            if node.id == 'True': return True
            if node.id == 'False': return False
            val = os.environ.get(node.id)
            if val is None:
                raise NameError(f"Environment variable '{node.id}' is not defined.")
            try:
                if val.lower().startswith('0x'): return int(val, 16)
                return float(val) if '.' in val else int(val)
            except ValueError:
                return val

        elif isinstance(node, ast.BinOp):
            return self.operators[type(node.op)](self._eval(node.left), self._eval(node.right))

        elif isinstance(node, ast.UnaryOp):
            return self.operators[type(node.op)](self._eval(node.operand))

        elif isinstance(node, ast.BoolOp):
            values = [self._eval(v) for v in node.values]
            result = values[0]
            for next_val in values[1:]:
                result = self.operators[type(node.op)](result, next_val)
            return result

        elif isinstance(node, ast.Compare):
            left_val = self._eval(node.left)
            right_val = self._eval(node.comparators[0])
            op_type = type(node.ops[0])
            if op_type not in self.operators:
                raise TypeError(f"Unsupported comparison operator: {op_type.__name__}")
            return self.operators[op_type](left_val, right_val)

        elif isinstance(node, ast.IfExp):
            # Use truthy helper for the condition
            return self._eval(node.body) if self._is_truthy(self._eval(node.test)) else self._eval(node.orelse)

        elif isinstance(node, ast.Call):
            if not isinstance(node.func, ast.Name):
                raise TypeError(f"Unsupported function call type: {type(node.func).__name__}")
            
            func_name = node.func.id
            if func_name in self.functions:
                func = self.functions[func_name]
                args = [self._eval(arg) for arg in node.args]
                
                # Factual check for argument counts where possible
                import inspect
                try:
                    sig = inspect.signature(func)
                    sig.bind(*args)
                except ValueError:
                    # Fallback for built-ins (min/max) that don't provide signatures
                    pass
                except TypeError:
                    # Actual parameter count mismatch
                    params = len(inspect.signature(func).parameters)
                    raise ValueError(f"Function '{func_name}' expects {params} arguments (got {len(args)})")
                
                return func(*args)
            raise ValueError(f"Forbidden function call: {func_name}")

        raise TypeError(f"Unsupported syntax: {type(node).__name__}")

    def run(self, expr):
        if not expr or not expr.strip(): return "Error: Empty expression"
        processed = expr.replace("&&", " and ").replace("||", " or ")
        processed = re.sub(r'!(?!=)', ' not ', processed)
        try:
            tree = ast.parse(processed.strip(), mode='eval')
            return self._eval(tree.body)
        except Exception as e:
            return f"Error: {e}"

if __name__ == "__main__":
    if len(sys.argv) < 2: sys.exit("Usage: ./evaluator 'expression'")
    print(Evaluator().run(sys.argv[1]))
