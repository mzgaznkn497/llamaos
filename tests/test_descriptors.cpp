#include "arch/x86_64/cpu/gdt.hpp"
#include "arch/x86_64/cpu/tss.hpp"
#include "arch/x86_64/cpu/idt.hpp"
#include "arch/x86_64/cpu/interrupts.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstddef>

// =============================================================================
// LlamaOS/A - Descriptors, IDT, and Interrupt Host Regression Test Suite
// =============================================================================

static size_t g_assertions_passed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "\n[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return false; \
        } \
        g_assertions_passed++; \
    } while (0)

using namespace llamaos::arch::x86_64;

// 1. Selector Constants
static bool test_selector_constants() {
    TEST_ASSERT(Selector::Null == 0x00, "Null selector must be 0x00");
    TEST_ASSERT(Selector::KernelCode == 0x08, "Kernel code selector must be 0x08");
    TEST_ASSERT(Selector::KernelData == 0x10, "Kernel data selector must be 0x10");
    TEST_ASSERT(Selector::Tss == 0x18, "TSS selector must be 0x18");
    return true;
}

// 2. GDT Descriptor Encoding
static bool test_gdt_descriptors() {
    // Kernel Code Descriptor
    GdtEntry code = encode_code_descriptor(0);
    uint8_t code_access = (code.raw >> 40) & 0xFF;
    uint8_t code_flags  = (code.raw >> 52) & 0x0F;
    uint32_t code_limit = (code.raw & 0xFFFF) | (((code.raw >> 48) & 0x0F) << 16);

    TEST_ASSERT((code_access & 0x80) != 0, "Code descriptor Present bit must be 1");
    TEST_ASSERT(((code_access >> 5) & 0x03) == 0, "Code descriptor DPL must be 0");
    TEST_ASSERT((code_access & 0x10) != 0, "Code descriptor S bit must be 1 (non-system)");
    TEST_ASSERT((code_access & 0x08) != 0, "Code descriptor Executable bit must be 1");
    TEST_ASSERT((code_access & 0x02) != 0, "Code descriptor Readable bit must be 1");
    TEST_ASSERT((code_flags & 0x02) != 0, "Code descriptor L bit must be 1 (64-bit Long Mode)");
    TEST_ASSERT((code_flags & 0x04) == 0, "Code descriptor D/B bit must be 0 in 64-bit");
    TEST_ASSERT((code_flags & 0x08) != 0, "Code descriptor Granularity bit must be 1");
    TEST_ASSERT(code_limit == 0xFFFFF, "Code descriptor limit must be 0xFFFFF");

    // Kernel Data Descriptor
    GdtEntry data = encode_data_descriptor(0);
    uint8_t data_access = (data.raw >> 40) & 0xFF;
    uint8_t data_flags  = (data.raw >> 52) & 0x0F;
    uint32_t data_limit = (data.raw & 0xFFFF) | (((data.raw >> 48) & 0x0F) << 16);

    TEST_ASSERT((data_access & 0x80) != 0, "Data descriptor Present bit must be 1");
    TEST_ASSERT(((data_access >> 5) & 0x03) == 0, "Data descriptor DPL must be 0");
    TEST_ASSERT((data_access & 0x10) != 0, "Data descriptor S bit must be 1 (non-system)");
    TEST_ASSERT((data_access & 0x08) == 0, "Data descriptor Executable bit must be 0");
    TEST_ASSERT((data_access & 0x02) != 0, "Data descriptor Writable bit must be 1");
    TEST_ASSERT((data_flags & 0x08) != 0, "Data descriptor Granularity bit must be 1");
    TEST_ASSERT(data_limit == 0xFFFFF, "Data descriptor limit must be 0xFFFFF");

    return true;
}

// 3. TSS Descriptor Encoding
static bool test_tss_descriptors() {
    uint64_t base = 0xFFFFFFFF70010000ULL;
    uint32_t limit = sizeof(TaskStateSegment) - 1; // 103 = 0x67

    GdtTssEntry tss = encode_tss_descriptor(base, limit, 0);

    // Reconstruct base
    uint64_t rec_base = ((tss.low >> 16) & 0xFFFFULL)
                      | (((tss.low >> 32) & 0xFFULL) << 16)
                      | (((tss.low >> 56) & 0xFFULL) << 24)
                      | ((tss.high & 0xFFFFFFFFULL) << 32);
    TEST_ASSERT(rec_base == base, "TSS base reconstruction mismatch");

    // Reconstruct limit
    uint32_t rec_limit = (tss.low & 0xFFFFULL) | (((tss.low >> 48) & 0x0FULL) << 16);
    TEST_ASSERT(rec_limit == limit, "TSS limit reconstruction mismatch");

    // Access byte: P=1, DPL=0, Type=0x9 (64-bit TSS available)
    uint8_t access = (tss.low >> 40) & 0xFF;
    TEST_ASSERT(access == 0x89, "TSS access byte must be 0x89 (Present, DPL 0, System, 64-bit TSS Available)");

    return true;
}

// 4. IDT Descriptor Encoding
static bool test_idt_descriptors() {
    uint64_t handler = 0xFFFFFFFF80108A40ULL;
    uint16_t selector = 0x08;

    // Interrupt Gate test
    IdtEntry gate1 = encode_idt_gate(handler, selector, GateType::InterruptGate, 0, 1);
    TEST_ASSERT(gate1.present == 1, "IDT gate must be present");
    TEST_ASSERT(gate1.selector == selector, "IDT gate selector mismatch");
    TEST_ASSERT(gate1.type == 0x0E, "Interrupt gate type must be 0x0E");
    TEST_ASSERT(gate1.dpl == 0, "IDT gate DPL must be 0");
    TEST_ASSERT(gate1.ist == 1, "IDT gate IST must be 1");
    TEST_ASSERT(gate1.handler_address() == handler, "IDT gate handler address reconstruction mismatch");

    // Trap Gate test
    IdtEntry gate2 = encode_idt_gate(handler + 0x10, selector, GateType::TrapGate, 3, 0);
    TEST_ASSERT(gate2.present == 1, "Trap gate must be present");
    TEST_ASSERT(gate2.type == 0x0F, "Trap gate type must be 0x0F");
    TEST_ASSERT(gate2.dpl == 3, "Trap gate DPL must be 3");
    TEST_ASSERT(gate2.ist == 0, "Trap gate IST must be 0");
    TEST_ASSERT(gate2.handler_address() == handler + 0x10, "Trap gate handler address reconstruction mismatch");

    return true;
}

// 5. Interrupt Frame Layout and Sizes
static bool test_interrupt_frame_layout() {
    TEST_ASSERT(sizeof(InterruptFrame) == 176, "InterruptFrame size must be exactly 176 bytes");
    TEST_ASSERT(offsetof(InterruptFrame, rax) == 0, "RAX offset must be 0");
    TEST_ASSERT(offsetof(InterruptFrame, rbx) == 8, "RBX offset must be 8");
    TEST_ASSERT(offsetof(InterruptFrame, r15) == 112, "R15 offset must be 112");
    TEST_ASSERT(offsetof(InterruptFrame, vector) == 120, "Vector offset must be 120");
    TEST_ASSERT(offsetof(InterruptFrame, error_code) == 128, "Error code offset must be 128");
    TEST_ASSERT(offsetof(InterruptFrame, rip) == 136, "RIP offset must be 136");
    TEST_ASSERT(offsetof(InterruptFrame, cs) == 144, "CS offset must be 144");
    TEST_ASSERT(offsetof(InterruptFrame, rflags) == 152, "RFLAGS offset must be 152");
    TEST_ASSERT(offsetof(InterruptFrame, rsp) == 160, "RSP offset must be 160");
    TEST_ASSERT(offsetof(InterruptFrame, ss) == 168, "SS offset must be 168");
    return true;
}

// 6. Exception Vector Metadata
static bool test_exception_metadata() {
    const ExceptionInfo* de = get_exception_info(0);
    TEST_ASSERT(de != nullptr && !de->has_error_code, "#DE must not have error code");

    const ExceptionInfo* bp = get_exception_info(3);
    TEST_ASSERT(bp != nullptr && !bp->has_error_code, "#BP must not have error code");

    const ExceptionInfo* ud = get_exception_info(6);
    TEST_ASSERT(ud != nullptr && !ud->has_error_code, "#UD must not have error code");

    const ExceptionInfo* df = get_exception_info(8);
    TEST_ASSERT(df != nullptr && df->has_error_code, "#DF must have error code");

    const ExceptionInfo* gp = get_exception_info(13);
    TEST_ASSERT(gp != nullptr && gp->has_error_code, "#GP must have error code");

    const ExceptionInfo* pf = get_exception_info(14);
    TEST_ASSERT(pf != nullptr && pf->has_error_code, "#PF must have error code");

    const ExceptionInfo* ac = get_exception_info(17);
    TEST_ASSERT(ac != nullptr && ac->has_error_code, "#AC must have error code");

    const ExceptionInfo* cp = get_exception_info(21);
    TEST_ASSERT(cp != nullptr && cp->has_error_code, "#CP must have error code");

    return true;
}

// 7. Error Code Classification
static bool test_error_code_classification() {
    // Page fault decodes
    PageFaultFlags pf0 = decode_page_fault_error_code(0x00);
    TEST_ASSERT(!pf0.present && !pf0.write && !pf0.user, "PF 0x00: non-present read supervisor");

    PageFaultFlags pf1 = decode_page_fault_error_code(0x01);
    TEST_ASSERT(pf1.present && !pf1.write && !pf1.user, "PF 0x01: protection violation read supervisor");

    PageFaultFlags pf2 = decode_page_fault_error_code(0x02);
    TEST_ASSERT(!pf2.present && pf2.write && !pf2.user, "PF 0x02: non-present write supervisor");

    PageFaultFlags pf3 = decode_page_fault_error_code(0x03);
    TEST_ASSERT(pf3.present && pf3.write && !pf3.user, "PF 0x03: protection violation write supervisor");

    PageFaultFlags pf7 = decode_page_fault_error_code(0x07);
    TEST_ASSERT(pf7.present && pf7.write && pf7.user, "PF 0x07: protection violation write user");

    PageFaultFlags pfnx = decode_page_fault_error_code(0x11);
    TEST_ASSERT(pfnx.present && !pfnx.write && pfnx.instruction_fetch, "PF 0x11: instruction fetch protection violation");

    PageFaultFlags pfall = decode_page_fault_error_code(0x807F);
    TEST_ASSERT(pfall.present && pfall.write && pfall.user && pfall.reserved_write &&
                pfall.instruction_fetch && pfall.protection_key && pfall.shadow_stack && pfall.sgx,
                "PF 0x807F: all flags decoded");

    // GPF decodes
    GpfFlags gpf0 = decode_gpf_error_code(0x00);
    TEST_ASSERT(!gpf0.external && !gpf0.idt && !gpf0.ldt && gpf0.selector_index == 0, "GPF 0x00");

    GpfFlags gpf1 = decode_gpf_error_code(0x01);
    TEST_ASSERT(gpf1.external && !gpf1.idt && gpf1.selector_index == 0, "GPF external");

    GpfFlags gpf_idt = decode_gpf_error_code(0x02);
    TEST_ASSERT(!gpf_idt.external && gpf_idt.idt && gpf_idt.selector_index == 0, "GPF IDT");

    GpfFlags gpf_sel = decode_gpf_error_code(0x0018); // Selector index 3 (0x18 >> 3)
    TEST_ASSERT(gpf_sel.selector_index == 3, "GPF selector index 3");

    return true;
}

int main() {
    printf("================================================================================\n");
    printf(" LlamaOS/A - Descriptor, IDT, and Exception Host Regression Test Suite\n");
    printf("================================================================================\n");

    bool success = true;
    success &= test_selector_constants();
    success &= test_gdt_descriptors();
    success &= test_tss_descriptors();
    success &= test_idt_descriptors();
    success &= test_interrupt_frame_layout();
    success &= test_exception_metadata();
    success &= test_error_code_classification();

    if (!success) {
        fprintf(stderr, "\n[REGRESSION FAILED] One or more descriptor tests failed!\n");
        return 1;
    }

    printf("\n[REGRESSION PASSED] All %zu assertions verified successfully.\n", g_assertions_passed);
    return 0;
}
