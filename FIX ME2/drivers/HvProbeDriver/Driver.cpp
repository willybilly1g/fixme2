// WDK diagnostic driver: privately-owned 4 KiB allocation, CPU feature probing,
// process PID existence check, synchronized IPI acknowledgement and clean unload.
// No arbitrary physical/process memory reads, no VMXON/VMRUN and no TLB invalidation.
#include <ntifs.h>
#include <wdmsec.h>
#include <intrin.h>
#include "hv/kernel_protocol.h"

namespace {
using namespace hv::kernel_protocol;
constexpr ULONG ioctl_version = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA);
constexpr ULONG page_tag = '7pVH';
struct LegacyReply { ULONG size, abi_version, feature_flags, reserved; };
const GUID class_guid = {0x164b5d67,0xa2d4,0x4179,{0xa3,0x11,0xa7,0xe2,0xc8,0x4c,0x61,0x39}};
UNICODE_STRING device_name = RTL_CONSTANT_STRING(L"\\Device\\HvLabProbe");
UNICODE_STRING link_name = RTL_CONSTANT_STRING(L"\\DosDevices\\HvLabProbe");
UNICODE_STRING allowed_sddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

struct RuntimeState {
    FAST_MUTEX lock;
    void* owned_page;
    ULONGLONG generation;
    ULONG feature_bits;
    ULONG vendor;
    ULONG processors;
    volatile LONG ipi_acks;
    ULONG phase;
    FramePayload frame;              // fixed, bounded; never reads other processes
    ULONGLONG frame_sequence;
    ULONGLONG frame_time_100ns;
    ULONG frame_publisher_pid;
};
RuntimeState state = {};

void probe_cpu() noexcept {
    int regs[4] = {};
    __cpuidex(regs, 0, 0);
    const unsigned max_basic = static_cast<unsigned>(regs[0]);
    const bool intel = regs[1] == 0x756e6547 && regs[3] == 0x49656e69 && regs[2] == 0x6c65746e;
    const bool amd = regs[1] == 0x68747541 && regs[3] == 0x69746e65 && regs[2] == 0x444d4163;
    state.vendor = intel ? vendor_intel : (amd ? vendor_amd : vendor_unknown);
    state.feature_bits = cpu_probe_complete;
    if (max_basic >= 1) {
        __cpuidex(regs, 1, 0);
        if (regs[2] & (1u << 31)) state.feature_bits |= host_hypervisor_present;
        if (intel && (regs[2] & (1u << 5))) state.feature_bits |= cpu_vmx_capable;
    }
    if (amd) {
        __cpuidex(regs, 0x80000000, 0);
        const unsigned max_extended = static_cast<unsigned>(regs[0]);
        if (max_extended >= 0x80000001) {
            __cpuidex(regs, 0x80000001, 0);
            if (regs[2] & (1u << 2)) state.feature_bits |= cpu_svm_capable;
        }
        if (max_extended >= 0x8000000A) {
            __cpuidex(regs, 0x8000000A, 0);
            if (regs[3] & 1) state.feature_bits |= cpu_npt_reported;
        }
    }
}

NTSTATUS initialize_runtime() noexcept {
    ExInitializeFastMutex(&state.lock);
    probe_cpu();
    state.processors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (!state.processors) return STATUS_NOT_SUPPORTED;
    // PAGE_SIZE allocation from nonpaged NX pool is 4096-aligned on supported x64 Windows.
    void* page = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, page_tag);
    if (!page) return STATUS_INSUFFICIENT_RESOURCES;
    const PHYSICAL_ADDRESS physical = MmGetPhysicalAddress(page); // Only OUR owned, resident page.
    if ((reinterpret_cast<ULONG_PTR>(page) & (PAGE_SIZE - 1)) != 0 ||
        (physical.QuadPart & (PAGE_SIZE - 1)) != 0) {
        ExFreePoolWithTag(page, page_tag);
        return STATUS_DATATYPE_MISALIGNMENT;
    }
    RtlZeroMemory(page, PAGE_SIZE);
    state.owned_page = page;
    state.feature_bits |= own_page_allocated;
    state.generation = 1;
    state.phase = phase_diagnostic_ready;
    state.frame_sequence = 0;
    state.frame_publisher_pid = 0;
    state.frame_time_100ns = 0;
    return STATUS_SUCCESS;
}

void shutdown_runtime() noexcept {
    ExAcquireFastMutex(&state.lock);
    state.phase = phase_stopping;
    RtlSecureZeroMemory(&state.frame,sizeof(state.frame));
    state.frame_sequence=0;
    state.frame_time_100ns=0;
    state.frame_publisher_pid=0;
    void* old = state.owned_page;
    state.owned_page = nullptr;
    state.feature_bits &= ~own_page_allocated;
    ++state.generation;
    ExReleaseFastMutex(&state.lock);
    if (old) {
        RtlSecureZeroMemory(old, PAGE_SIZE);
        ExFreePoolWithTag(old, page_tag);
    }
}

ULONG_PTR NTAPI ipi_acknowledge(ULONG_PTR) {
    // IPI_LEVEL: no memory allocation, waits, pageable code or Windows object APIs.
    InterlockedIncrement(&state.ipi_acks);
    return 0;
}

NTSTATUS finish(PIRP irp, NTSTATUS code, ULONG_PTR bytes) noexcept {
    irp->IoStatus.Status = code;
    irp->IoStatus.Information = bytes;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return code;
}
NTSTATUS unsupported(PDEVICE_OBJECT, PIRP irp) noexcept {
    return finish(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
}
NTSTATUS open_close(PDEVICE_OBJECT, PIRP irp) noexcept {
    return finish(irp, STATUS_SUCCESS, 0);
}
NTSTATUS control(PDEVICE_OBJECT, PIRP irp) noexcept {
    const PIO_STACK_LOCATION io = IoGetCurrentIrpStackLocation(irp);
    const ULONG ctl = io->Parameters.DeviceIoControl.IoControlCode;
    const ULONG in_size = io->Parameters.DeviceIoControl.InputBufferLength;
    const ULONG out_size = io->Parameters.DeviceIoControl.OutputBufferLength;
    void* const buffer = irp->AssociatedIrp.SystemBuffer;
    if (ctl == ioctl_version) {
        if (!buffer || out_size < sizeof(LegacyReply)) return finish(irp, STATUS_BUFFER_TOO_SMALL, 0);
        *static_cast<LegacyReply*>(buffer) = {sizeof(LegacyReply), 1, 0, 0};
        return finish(irp, STATUS_SUCCESS, sizeof(LegacyReply));
    }
    // Admin-only demo telemetry channel: no arbitrary process or physical memory reads.
    if (ctl == ioctl_frame_publish) {
        if (KeGetCurrentIrql()!=PASSIVE_LEVEL) return finish(irp,STATUS_INVALID_DEVICE_STATE,0);
        if (!buffer || in_size!=sizeof(FramePublish) || out_size<sizeof(FrameAck))
            return finish(irp,STATUS_INVALID_PARAMETER,0);
        const FramePublish input=*static_cast<FramePublish*>(buffer);
        if (input.header.size!=sizeof(input) || input.header.version!=abi_version ||
            !input.header.request_id || !input.frame_sequence || !validate_payload(&input.payload)) return finish(irp,STATUS_INVALID_PARAMETER,0);
        const ULONG pid=HandleToULong(PsGetCurrentProcessId());
        ExAcquireFastMutex(&state.lock);
        if (state.phase!=phase_diagnostic_ready || !state.owned_page) {
            ExReleaseFastMutex(&state.lock);
            return finish(irp,STATUS_DEVICE_NOT_READY,0);
        }
        if (pid==state.frame_publisher_pid && input.frame_sequence<=state.frame_sequence) {
            ExReleaseFastMutex(&state.lock);
            return finish(irp,STATUS_INVALID_PARAMETER,0);
        }
        state.frame=input.payload;
        state.frame_sequence=input.frame_sequence;
        state.frame_time_100ns=KeQueryInterruptTime();
        state.frame_publisher_pid=pid;
        ++state.generation;
        ExReleaseFastMutex(&state.lock);
        FrameAck* out=static_cast<FrameAck*>(buffer);
        *out={sizeof(FrameAck),abi_version,input.header.request_id,static_cast<ULONG>(Code::Ok),pid,input.frame_sequence};
        return finish(irp,STATUS_SUCCESS,sizeof(FrameAck));
    }
    if (ctl == ioctl_frame_read) {
        if (KeGetCurrentIrql()!=PASSIVE_LEVEL) return finish(irp,STATUS_INVALID_DEVICE_STATE,0);
        if (!buffer || in_size!=sizeof(Request) || out_size<sizeof(FrameReply))
            return finish(irp,STATUS_BUFFER_TOO_SMALL,0);
        const Request input=*static_cast<Request*>(buffer);
        if (input.size!=sizeof(input) || input.version!=abi_version || !input.request_id)
            return finish(irp,STATUS_INVALID_PARAMETER,0);
        FrameReply result={};
        result.size=sizeof(result);result.version=abi_version;result.request_id=input.request_id;
        result.code=static_cast<ULONG>(Code::NotFound);
        ExAcquireFastMutex(&state.lock);
        if (state.phase==phase_diagnostic_ready && state.frame_publisher_pid && state.frame_sequence &&
            state.frame_time_100ns && KeQueryInterruptTime()-state.frame_time_100ns<=5000000ull) {
            result.code=static_cast<ULONG>(Code::Ok);
            result.publisher_pid=state.frame_publisher_pid;
            result.frame_sequence=state.frame_sequence;
            result.timestamp_100ns=state.frame_time_100ns;
            result.payload=state.frame;
        }
        ExReleaseFastMutex(&state.lock);
        RtlCopyMemory(buffer,&result,sizeof(result));
        return finish(irp,STATUS_SUCCESS,sizeof(result));
    }
    if (ctl != ioctl_status && ctl != ioctl_process && ctl != ioctl_rectangle && ctl != ioctl_runtime)
        return finish(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (!buffer || in_size < sizeof(Request)) return finish(irp, STATUS_INVALID_PARAMETER, 0);
    const Request request = *static_cast<Request*>(buffer); // METHOD_BUFFERED: copy before output.
    if (request.version != abi_version || !request.request_id) return finish(irp, STATUS_REVISION_MISMATCH, 0);
    if (ctl == ioctl_status || ctl == ioctl_rectangle || ctl == ioctl_runtime) {
        if (request.size != sizeof(Request)) return finish(irp, STATUS_INVALID_PARAMETER, 0);
        if (ctl == ioctl_status) {
            if (out_size < sizeof(StatusReply)) return finish(irp, STATUS_BUFFER_TOO_SMALL, 0);
            StatusReply* r = static_cast<StatusReply*>(buffer);
            RtlZeroMemory(r, sizeof(*r));
            r->size = sizeof(*r); r->version = abi_version; r->request_id = request.request_id;
            r->code = static_cast<ULONG>(Code::Ok);
            r->flags = driver_loaded | transport_ready;
            r->logical_processors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
            return finish(irp, STATUS_SUCCESS, sizeof(*r));
        }
        if (ctl == ioctl_rectangle) {
            if (out_size < sizeof(RectangleReply)) return finish(irp, STATUS_BUFFER_TOO_SMALL, 0);
            RectangleReply* r = static_cast<RectangleReply*>(buffer);
            RtlZeroMemory(r, sizeof(*r));
            r->size = sizeof(*r); r->version = abi_version; r->request_id = request.request_id;
            r->code = static_cast<ULONG>(Code::Ok);
            r->x = 180; r->y = 150; r->width = 120; r->height = 240; r->health = 75;
            r->source = 1; // synthetic visual diagnostic; not game data.
            return finish(irp, STATUS_SUCCESS, sizeof(*r));
        }
        if (out_size < sizeof(RuntimeReply)) return finish(irp, STATUS_BUFFER_TOO_SMALL, 0);
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) return finish(irp, STATUS_INVALID_DEVICE_STATE, 0);
        RuntimeReply result = {};
        ExAcquireFastMutex(&state.lock);
        if (state.phase != phase_diagnostic_ready || !state.owned_page) {
            ExReleaseFastMutex(&state.lock);
            return finish(irp, STATUS_DEVICE_NOT_READY, 0);
        }
        state.ipi_acks = 0;
        // Do not hold FAST_MUTEX (APC_LEVEL) across KeIpiGenericCall rendezvous.
        ExReleaseFastMutex(&state.lock);
        KeIpiGenericCall(ipi_acknowledge, 0);
        ExAcquireFastMutex(&state.lock);
        if (state.phase!=phase_diagnostic_ready || !state.owned_page) {
            ExReleaseFastMutex(&state.lock);
            return finish(irp,STATUS_DEVICE_NOT_READY,0);
        }
        const ULONG acks = static_cast<ULONG>(state.ipi_acks);
        const ULONG processors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
        result.size = sizeof(result); result.version = abi_version; result.request_id = request.request_id;
        result.code = static_cast<ULONG>(Code::Ok); result.phase = state.phase;
        result.features = state.feature_bits;
        if (acks == processors) result.features |= ipi_diagnostic_complete;
        result.vendor = state.vendor; result.logical_processors = processors;
        result.ipi_acks = acks; result.owned_pages = 1; result.page_size = PAGE_SIZE;
        result.generation = state.generation;
        ExReleaseFastMutex(&state.lock);
        RtlCopyMemory(buffer, &result, sizeof(result));
        return finish(irp, STATUS_SUCCESS, sizeof(result));
    }
    if (in_size < sizeof(ProcessRequest) || out_size < sizeof(ProcessReply) || request.size != sizeof(ProcessRequest))
        return finish(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return finish(irp, STATUS_INVALID_DEVICE_STATE, 0);
    const ProcessRequest proc = *static_cast<ProcessRequest*>(buffer);
    if (!proc.pid || proc.reserved) return finish(irp, STATUS_INVALID_PARAMETER, 0);
    PEPROCESS process = nullptr;
    const NTSTATUS looked_up = PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(proc.pid)), &process);
    if (NT_SUCCESS(looked_up)) ObDereferenceObject(process); // reference released in every path.
    ProcessReply* r = static_cast<ProcessReply*>(buffer);
    RtlZeroMemory(r, sizeof(*r));
    r->size = sizeof(*r); r->version = abi_version; r->request_id = proc.header.request_id;
    r->pid = proc.pid; r->found = NT_SUCCESS(looked_up) ? 1u : 0u;
    r->code = static_cast<ULONG>(NT_SUCCESS(looked_up) ? Code::Ok : Code::NotFound);
    return finish(irp, STATUS_SUCCESS, sizeof(*r));
}
VOID unload(PDRIVER_OBJECT driver) {
    IoDeleteSymbolicLink(&link_name);
    if (driver->DeviceObject) IoDeleteDevice(driver->DeviceObject);
    shutdown_runtime();
}
} // namespace

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING) {
    NTSTATUS result = initialize_runtime();
    if (!NT_SUCCESS(result)) return result;
    for (ULONG i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) driver->MajorFunction[i] = unsupported;
    driver->MajorFunction[IRP_MJ_CREATE] = open_close;
    driver->MajorFunction[IRP_MJ_CLOSE] = open_close;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = control;
    driver->DriverUnload = unload;
    PDEVICE_OBJECT dev = nullptr;
    result = IoCreateDeviceSecure(driver, 0, &device_name, FILE_DEVICE_UNKNOWN,
                                  FILE_DEVICE_SECURE_OPEN, FALSE, &allowed_sddl, &class_guid, &dev);
    if (!NT_SUCCESS(result)) { shutdown_runtime(); return result; }
    dev->Flags |= DO_BUFFERED_IO;
    result = IoCreateSymbolicLink(&link_name, &device_name);
    if (!NT_SUCCESS(result)) {
        IoDeleteDevice(dev);
        shutdown_runtime();
        return result;
    }
    dev->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
