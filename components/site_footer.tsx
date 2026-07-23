import Link from "next/link";

export function SiteFooter() {
  return (
    <footer>
      <div>
        <span className="brandMark">OS</span>
        <p>
          x86-64 OS LAB
          <br />
          BUILD EVERYTHING. UNDERSTAND EVERYTHING.
        </p>
      </div>
      <p className="footerNote">
        <Link href="/docs/">DOCS / PROJECT STATUS: PLANNING</Link>
        <br />
        QEMU SIMULATES HARDWARE. THE REST IS OURS.
      </p>
    </footer>
  );
}
