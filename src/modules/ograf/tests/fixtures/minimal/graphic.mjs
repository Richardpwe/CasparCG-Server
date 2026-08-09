export default class MinimalGraphic extends HTMLElement {
    async load({data = {}} = {}) {
        this.textContent = data.message ?? "Hello World!";
    }

    async playAction() {
        return {statusCode: 200, statusMessage: "OK", currentStep: 0};
    }

    async updateAction({data = {}} = {}) {
        if (data.message !== undefined) {
            this.textContent = data.message;
        }
        return {statusCode: 200, statusMessage: "OK"};
    }

    async stopAction() {
        return {statusCode: 200, statusMessage: "OK"};
    }

    async customAction() {
        return {statusCode: 400, statusMessage: "No custom actions supported"};
    }

    async dispose() {
    }
}
