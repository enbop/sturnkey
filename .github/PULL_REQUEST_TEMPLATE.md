## Summary

Describe the user-visible or architectural change.

## Verification

- [ ] `cmake --preset dev`
- [ ] `cmake --build --preset dev --target sturnkey`
- [ ] `ctest --preset dev --output-on-failure`
- [ ] Relevant formatting or component validation checks

## Runtime impact

Note any new WIT imports, Wasmtime capabilities, JavaScript APIs, or upstream
revision changes. Write `None` when this does not apply.
