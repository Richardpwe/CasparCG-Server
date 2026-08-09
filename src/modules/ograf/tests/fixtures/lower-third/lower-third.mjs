export default class LowerThirdGraphic extends HTMLElement {
    constructor() {
        super();
        this.initialized = false;
    }

    connectedCallback() {
        if (this.initialized) {
            return;
        }
        this.initialized = true;
        Object.assign(this.style, {
            background: "linear-gradient(135deg, rgba(8, 36, 72, 0.96), rgba(14, 91, 145, 0.96))",
            borderLeft: "12px solid #35b9ff",
            bottom: "8%",
            boxShadow: "0 12px 36px rgba(0, 0, 0, 0.45)",
            boxSizing: "border-box",
            color: "#ffffff",
            display: "block",
            fontFamily: "Arial, Helvetica, sans-serif",
            fontSize: "clamp(32px, 4vw, 72px)",
            fontWeight: "700",
            inset: "auto auto 8% 5%",
            letterSpacing: "0.01em",
            lineHeight: "1.1",
            maxWidth: "80%",
            opacity: "0",
            padding: "24px 40px",
            position: "absolute",
            textShadow: "0 2px 4px rgba(0, 0, 0, 0.35)",
            transform: "translateY(24px)",
            transition: "opacity 220ms ease, transform 220ms ease",
            whiteSpace: "nowrap"
        });
    }

    setVisible(visible, skipAnimation = false) {
        this.style.transition = skipAnimation
            ? "none"
            : "opacity 220ms ease, transform 220ms ease";
        this.style.opacity = visible ? "1" : "0";
        this.style.transform = visible
            ? "translateY(0)"
            : "translateY(24px)";
    }

    async load({data = {}} = {}) {
        this.textContent = data.name ?? "John Doe";
        this.currentStep = undefined;
        this.setVisible(false, true);
    }

    async playAction({delta, goto, skipAnimation = false} = {}) {
        const currentStep = Number.isInteger(this.currentStep)
            ? this.currentStep
            : -1;
        const targetStep = Number.isInteger(goto) && goto >= 0
            ? goto
            : currentStep + (Number.isInteger(delta) ? delta : 1);
        this.currentStep = targetStep === 0
            ? 0
            : undefined;
        this.setVisible(this.currentStep === 0, skipAnimation);
        return {statusCode: 200, statusMessage: "OK", currentStep: this.currentStep};
    }

    async stopAction({skipAnimation = false} = {}) {
        this.currentStep = undefined;
        this.setVisible(false, skipAnimation);
        return {statusCode: 200, statusMessage: "OK", currentStep: this.currentStep};
    }

    async updateAction({data = {}} = {}) {
        if (data.name !== undefined) {
            this.textContent = data.name;
        }
        return {statusCode: 200, statusMessage: "OK"};
    }

    async customAction() {
        return {statusCode: 400, statusMessage: "No custom actions supported"};
    }

    async dispose() {
        this.setVisible(false, true);
    }
}
