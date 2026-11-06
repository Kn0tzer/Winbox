# FD-165 — the xattr write forms classify names as the host does, and recognised namespaces are stored

Branch `lane-fd165` on base `c4dd96ecd` (the FD-162..164
lane's tip, itself on the batch-7 integration tip
`4006ddb58`). Tests `09bd3f452`, fix `a2899c5b5`,
follow-up `37dba7b69`. Lane-local closure CL-122
(provisional; the number settles at the next composition —
CL-119..CL-121 are claimed by two earlier lanes and the
audit settles all of them). Evidence in
`test-results/fd165/`.

## Host measurement first

The host behaviour was measured at the raw syscall level
before any fix: hand-written syscall instructions (the
asm-generic numbers this kernel interface uses), never the
C library wrappers, cross-checked with strace so the shape
that enters the kernel is the shape that was measured
(`hostref-fd165.out`, `hostref-fd165b/c/d.out`; sources
`hostref-fd165*.c` alongside). Four rounds: the set/remove
classification across every name class on a resolved file;
the entry order (flags, name fetch, the value copy, target
resolution) against null values, missing paths and bad
descriptors; the CREATE/REPLACE verdicts and storage
read-back through the get/list forms; and the link itself
as a target through the l-forms, plus directory targets.

What the host does, in order: the set forms refuse a flags
word outside 0x3 (-EINVAL); fetch the name (null -EFAULT,
empty -ERANGE); copy the value (a null value with a nonzero
size faults, -EFAULT, ahead of every target verdict); then
resolve the target (null path -EFAULT, missing -ENOENT,
through a file -ENOTDIR, bad descriptor -EBADF); only then
classify the name. On a resolved file or directory: an
unknown family and "system.*" outside the two posix_acl
names answer -ENOTSUP; a bare recognised prefix
("user."/"trusted."/"security.") answers -EINVAL; the
posix_acl names answer -EINVAL to a set carrying a non-ACL
value, while removing one is a no-op success; a recognised
namespace with an attribute part is stored (answer 0), with
CREATE on a present attribute answering -EEXIST and REPLACE
on an absent one -ENODATA — and the classification verdict
precedes the existence verdicts. On the link itself every
"user.*" name answers -EPERM (ahead of even the bare-prefix
check), "trusted.*"/"security.*" behave exactly as on a
file, and a posix_acl remove answers -ENOTSUP instead of
the file's success. The descriptor remove form for the
modelled name on a file that does not carry it — the one
shape the filing named — answers -ENODATA, as the guest
already did there.

## The defect, by substance

### FD-165 — beyond the one modelled name, the set/remove forms answered "unsupported" for every name

Both dispatchers answered their standing -ENOTSUP for every
name on a resolved target (the 32-bit path forms for all
names, the modelled one included), so a guest could not
tell "this namespace is not supported" from "this name is
malformed", "this value is not an ACL", "that attribute is
already there", "that attribute is not there", or plain
success — the host gives each of those its own answer, and
accepts and stores the recognised namespaces outright. The
set forms also never faulted on a null value the way the
host's value copy does. Two smaller wrong answers rode
along: the 32-bit descriptor set form stored the modelled
attribute and still reported -ENOTSUP, and the 64-bit get
forms never served the modelled attribute at all.

The write forms now run the measured order on both
dispatchers (one classifier per dispatcher, kept in step by
hand, as they are separate translation units): flags, name
fetch, value copy, target resolution, then the link rule,
the posix_acl rule, and the namespace verdict. Recognised
namespaces are stored — one native sidecar file per
attribute beside the modelled attribute's own sidecar, with
the attribute name %-escaped in the sidecar's name — so a
set that answers 0 reads back: the get forms serve stored
values with the host's size semantics and the list forms
serve the stored names, on both widths; CREATE/REPLACE
verdicts apply to stored names including the modelled one.
Sidecars never appear in directory listings, and they move
and delete with their file, the way the modelled
attribute's sidecar always did. A store on a target whose
backing cannot hold a sidecar refuses -ENOTSUP and logs
loudly once, the standing pattern for an honest refusal.
Guest tests `testXattrWriteClassify32Syscalls` and
`testXattrWriteClassify64Syscalls`; the FD-157/FD-162-era
xattr tests' standing expectations for resolved set/remove
forms moved to the measured answers in the same commit as
the new tests.

Not in this item's scope, by the filing's terms and the
measurement's bounds: the host's name-length cap and
-E2BIG (unmeasured), real ACL storage (a conforming ACL
value would be accepted by the host; the guest stores none
and refuses a non-ACL value as the host does), xattrs on
pipe/socket descriptors (unmeasured; the standing -ENOTSUP
stands there), and the 32-bit modelled-name get form's
exact-fit edge (standing behaviour, untouched).

## Proof

RED on the base (tests committed first, `09bd3f452`): the
two new write-classification tests fail in the filed shapes
and the three updated standing tests fail with them — main
3 tests FAILED (`red-main.log`: e.g. `setxattr(a.txt,
user.) returned -95 (expected -22)`, `fremovexattr(fileFd,
user.z) returned -95 (expected 0)`), x64 2 tests FAILED
(`red-x64.log`). GREEN on the fixed tree: full native
suites run serially with 0 FAILED (main 690 tests, x64 114
tests — `green2-main.log`, `green2-x64.log`); the JS unit
suite passes 353/0 (`js-unit2.log`); the browser build
verifies on the final tree ('Browser export verified',
BUILD COMPLETE, wasm `521937b979`, built from `37dba7b69`
with a clean tree — `browser-build2.log`).

The first full JS run on the fix found two real things,
both fixed in the follow-up (`37dba7b69`) and re-proven by
the runs above. The silent-failure scan counted one new
unlogged -EINVAL return in the 64-bit dispatcher — the set
forms' posix_acl refusal; ACL values are genuinely
unmodelled (the host would accept a conforming ACL), so
the refusal now reports through klog_fmt in both
dispatchers rather than answering silently, with the
guest-visible answer unchanged. And the
directory-publication unit test compiles the
directory-loading function standalone against a stub
header that replicates the sidecar suffix defines itself;
the new skip in that function names the new suffix, so the
stub replicates it too — the same slice-harness shape the
FD-162..164 lane hit in the neighbouring slice test.

## New filings from this lane's measurements

None. Every other shape the four rounds measured matched
the behaviour the design above now implements or a standing
answer the controls already pinned (the get/list size
semantics, the entry order's earlier stages, the modelled
name's -ENODATA on the descriptor remove form).

## Setup note

The working copy was set up from the FD-162..164 lane's
tree (payload inputs copied across: node_modules symlink,
dxvk, gstreamer and vkd3d payloads, the untracked Slang
assets), the runtime assets restored from the retained
local archive (`tools/browser/bootstrap.py --local
/home/ubuntu/backups/winbox-runtime-assets-20261104/
browser-runtime-20261104.tar`, the pinned release URL
answering 404 — FD-29, accepted), and `stage-vendor.mjs`
run before any JS result was read. Builds on this box are
shared and slow under the other lanes' load; the lane's
first build chain stalled when its SSH session dropped
(the make sat on a jobserver pipe with no tokens) and was
restarted detached with stdin closed, the shape the other
lanes use — after which every build and suite ran detached
and was read back from its log.
