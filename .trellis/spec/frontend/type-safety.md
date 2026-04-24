# Type Safety

The interface layer is C++, so type safety means explicit signatures and narrow conversions.

## Rules
- Expose concrete types such as `std::string`, `bool`, and explicit pointers or refs.
- Use `py::arg(...)` for named Python parameters.
- Keep Tcl option names centralized and validate required values before use.
- Convert external strings to typed CTS config fields inside backend config code.

## Examples
- `src/interface/python/py_icts/py_register_icts.h`
- `src/interface/tcl/tcl_icts/tcl_cts.h`
- `src/operation/iCTS/source/data_manager/config/CtsConfig.hh`
- `src/operation/iCTS/source/data_manager/config/JsonParser.cc`

## Avoid
- Generic `void*` or stringly-typed plumbing across multiple layers.
- Re-parsing the same string input in every wrapper.
- Introducing a wrapper-only naming scheme for the same backend field.
