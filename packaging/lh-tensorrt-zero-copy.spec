Name:           lh-tensorrt-zero-copy
Version:        0.1.0
Release:        1%{?dist}
Summary:        lh-trt-zerocopy - Hardware Pitch Alignment & SMMU Page Floor Invariant
License:        Apache-2.0
URL:            https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-tensorrt-zero-copy
Source0:        %{name}-%{version}-src.tar.gz
Packager:       Sameer Tawade <ops@logihertz.com>
BuildArch:      %{_arch}

%description
C++17 V4L2-to-TensorRT zero-copy DMA-BUF shared memory allocator enforcing 256-byte pitch & 4KB SMMU page alignment, lock-free capture-to-inference syncpoint fences, and BT.709 NV12-to-NCHW tensor conversion.

%prep
%autosetup -n %{name}-0.1.0

%build
# Industrial clean-room build with zero external runtime dependencies

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_docdir}/%{name}
mkdir -p %{buildroot}%{_mandir}/man1

if [ -f bin/lh-trt-zerocopy ]; then
    install -m 0755 bin/lh-trt-zerocopy %{buildroot}%{_bindir}/lh-trt-zerocopy
elif [ -f lh-trt-zerocopy ]; then
    install -m 0755 lh-trt-zerocopy %{buildroot}%{_bindir}/lh-trt-zerocopy
fi

install -m 0644 README.md %{buildroot}%{_docdir}/%{name}/README.md
install -m 0644 SPEC.md %{buildroot}%{_docdir}/%{name}/SPEC.md
install -m 0644 LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
install -m 0644 NOTICE %{buildroot}%{_docdir}/%{name}/NOTICE

%files
%doc %{_docdir}/%{name}/README.md
%doc %{_docdir}/%{name}/SPEC.md
%doc %{_docdir}/%{name}/NOTICE
%license %{_docdir}/%{name}/LICENSE

%changelog
* Mon Nov 16 2026 Sameer Tawade <ops@logihertz.com> - 0.1.0-1
- Initial production release for lh-tensorrt-zero-copy.
