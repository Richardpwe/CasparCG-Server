export default class LowerThirdGraphic extends HTMLElement {
    async load({data = {}} = {}) {
        this.textContent = data.name ?? "John Doe";
        this.currentStep = 0;
    }

    async playAction({delta, goto} = {}) {
        this.currentStep = Number.isInteger(goto)
            ? goto
            : this.currentStep + (Number.isInteger(delta) ? delta : 1);
        return {statusCode: 200, statusMessage: "OK", currentStep: this.currentStep};
    }

    async stopAction() {
        return {statusCode: 200, statusMessage: "OK"};
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
    }
}
