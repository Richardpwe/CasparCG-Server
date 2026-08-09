import assert from "node:assert/strict";
import {readFile, rm, writeFile} from "node:fs/promises";
import {tmpdir} from "node:os";
import {pathToFileURL} from "node:url";
import path from "node:path";

const hostPath = process.argv[2];
const fixtureRoot = process.argv[3];
if (!hostPath || !fixtureRoot) {
    throw new Error("Expected the OGraf host HTML path and fixture root");
}

const html = await readFile(hostPath, "utf8");
const script = html.match(/<script type="module">([\s\S]*?)<\/script>/)?.[1];
if (!script) {
    throw new Error("Could not find the OGraf host module");
}

const suffix = `${process.pid}-${Date.now()}`;
const hostModulePath = path.join(tmpdir(), `casparcg-ograf-host-${suffix}.mjs`);
const graphicModulePath = path.join(tmpdir(), `casparcg-ograf-graphic-${suffix}.mjs`);

const responses = [];
const readyMessages = [];
const calls = [];
const definitions = new Map();
const root = {
    children: [],
    appendChild(element) {
        this.children.push(element);
        element.parent = this;
        element.connectedCallback?.();
    }
};

globalThis.__ografTestCalls = calls;
globalThis.__ografTestDelay = delay => new Promise(resolve => setTimeout(resolve, delay));
globalThis.__ografTestStress = false;
globalThis.HTMLElement = class {
    constructor() {
        this.dataset = {};
        this.style = {};
    }

    remove() {
        if (this.parent) {
            this.parent.children = this.parent.children.filter(element => element !== this);
            this.parent = undefined;
        }
    }
};
globalThis.customElements = {
    define(name, constructor) {
        if ([...definitions.values()].includes(constructor)) {
            throw new Error("A Custom Element constructor was registered twice");
        }
        definitions.set(name, constructor);
    }
};
globalThis.document = {
    getElementById() {
        return root;
    },
    createElement(name) {
        const Constructor = definitions.get(name);
        assert.ok(Constructor, `Unknown Custom Element ${name}`);
        return new Constructor();
    }
};
globalThis.window = globalThis;
globalThis.casparNative = {
    postMessage(value) {
        const message = JSON.parse(value);
        if (message.type === "ready") {
            readyMessages.push(message);
        } else {
            responses.push(message);
        }
    }
};

const graphicModule = `
export default class Graphic extends globalThis.HTMLElement {
    async load(params) {
        globalThis.__ografTestCalls.push(["load", this.dataset.graphicInstanceId, params]);
        await globalThis.__ografTestDelay(globalThis.__ografTestStress ? 0 : 10);
        return {statusCode: 200, statusMessage: "loaded"};
    }
    async playAction(params) {
        globalThis.__ografTestCalls.push(["playAction", this.dataset.graphicInstanceId, params]);
        await globalThis.__ografTestDelay(globalThis.__ografTestStress ? 0 : 30);
        return {statusCode: 200, currentStep: 2};
    }
    async updateAction(params) {
        globalThis.__ografTestCalls.push(["updateAction", this.dataset.graphicInstanceId, params]);
        await globalThis.__ografTestDelay(globalThis.__ografTestStress ? 0 : 5);
        return {statusCode: 200};
    }
    async stopAction(params) {
        globalThis.__ografTestCalls.push(["stopAction", this.dataset.graphicInstanceId, params]);
        return {statusCode: 200};
    }
    async customAction(params) {
        globalThis.__ografTestCalls.push(["customAction", this.dataset.graphicInstanceId, params]);
        return {statusCode: 200, result: params.payload};
    }
    async dispose() {
        globalThis.__ografTestCalls.push(["dispose", this.dataset.graphicInstanceId]);
        return {statusCode: 200};
    }
}
`;

async function waitForResponseCount(count) {
    const deadline = Date.now() + 2000;
    while (responses.length < count && Date.now() < deadline) {
        await new Promise(resolve => setTimeout(resolve, 5));
    }
    assert.equal(responses.length, count, `Expected ${count} bridge responses`);
}

try {
    await writeFile(hostModulePath, script);
    await writeFile(graphicModulePath, graphicModule);
    await import(`${pathToFileURL(hostModulePath).href}?${suffix}`);
    assert.equal(readyMessages.length, 1);
    const moduleUrl = pathToFileURL(graphicModulePath).href;

    window.__casparOgraphDispatch({
        requestId: "load-a",
        operation: "load",
        graphicInstanceId: "instance-a",
        moduleUrl,
        params: {data: {name: "Ada"}}
    });
    window.__casparOgraphDispatch({
        requestId: "load-b",
        operation: "load",
        graphicInstanceId: "instance-b",
        moduleUrl,
        params: {data: {name: "Grace"}}
    });
    await waitForResponseCount(2);

    assert.ok(responses.every(response => response.ok));
    assert.equal(definitions.size, 1);
    assert.equal(root.children.length, 2);

    window.__casparOgraphDispatch({
        requestId: "play",
        operation: "playAction",
        graphicInstanceId: "instance-a",
        params: {delta: 1}
    });
    window.__casparOgraphDispatch({
        requestId: "update",
        operation: "updateAction",
        graphicInstanceId: "instance-a",
        params: {data: {name: "Katherine"}}
    });
    await waitForResponseCount(4);

    assert.equal(responses[2].requestId, "update");
    assert.equal(responses[3].requestId, "play");
    assert.equal(responses[3].value.currentStep, 2);

    window.__casparOgraphDispatch({
        requestId: "stop",
        operation: "stopAction",
        graphicInstanceId: "instance-a",
        params: {skipAnimation: true}
    });
    window.__casparOgraphDispatch({
        requestId: "custom",
        operation: "customAction",
        graphicInstanceId: "instance-a",
        params: {id: "highlight", payload: {color: "red"}}
    });
    await waitForResponseCount(6);
    assert.deepEqual(responses[5].value.result, {color: "red"});

    window.__casparOgraphDispatch({
        requestId: "dispose-a",
        operation: "dispose",
        graphicInstanceId: "instance-a",
        params: {}
    });
    window.__casparOgraphDispatch({
        requestId: "dispose-b",
        operation: "dispose",
        graphicInstanceId: "instance-b",
        params: {}
    });
    await waitForResponseCount(8);
    assert.equal(root.children.length, 0);
    assert.equal(calls.filter(([operation]) => operation === "dispose").length, 2);

    globalThis.__ografTestStress = true;
    let expectedResponses = responses.length;
    for (let cycle = 0; cycle < 100; ++cycle) {
        const graphicInstanceId = `stress-${cycle}`;
        window.__casparOgraphDispatch({
            requestId: `stress-load-${cycle}`,
            operation: "load",
            graphicInstanceId,
            moduleUrl,
            params: {data: {cycle}}
        });
        await waitForResponseCount(++expectedResponses);

        window.__casparOgraphDispatch({
            requestId: `stress-play-${cycle}`,
            operation: "playAction",
            graphicInstanceId,
            params: {delta: 1}
        });
        await waitForResponseCount(++expectedResponses);

        window.__casparOgraphDispatch({
            requestId: `stress-stop-${cycle}`,
            operation: "stopAction",
            graphicInstanceId,
            params: {skipAnimation: true}
        });
        await waitForResponseCount(++expectedResponses);

        window.__casparOgraphDispatch({
            requestId: `stress-dispose-${cycle}`,
            operation: "dispose",
            graphicInstanceId,
            params: {}
        });
        await waitForResponseCount(++expectedResponses);
        assert.equal(root.children.length, 0);
    }
    assert.equal(responses.length, 408);
    assert.ok(responses.every(response => response.ok));

    const smokeFixtures = [
        {
            id: "minimal",
            modulePath: path.join(fixtureRoot, "minimal", "graphic.mjs"),
            data: {message: "Hello from CasparCG"},
            expectedText: "Hello from CasparCG"
        },
        {
            id: "lower-third",
            modulePath: path.join(fixtureRoot, "lower-third", "lower-third.mjs"),
            data: {name: "Ada Lovelace"},
            expectedText: "Ada Lovelace",
            expectedColor: "#ffffff"
        }
    ];
    for (const fixture of smokeFixtures) {
        const graphicInstanceId = `fixture-${fixture.id}`;
        window.__casparOgraphDispatch({
            requestId: `fixture-load-${fixture.id}`,
            operation: "load",
            graphicInstanceId,
            moduleUrl: pathToFileURL(fixture.modulePath).href,
            params: {data: fixture.data}
        });
        await waitForResponseCount(++expectedResponses);
        assert.equal(root.children.length, 1);
        assert.equal(root.children[0].textContent, fixture.expectedText);
        if (fixture.expectedColor) {
            assert.equal(root.children[0].style.color, fixture.expectedColor);
            assert.equal(root.children[0].style.opacity, "0");
        }

        window.__casparOgraphDispatch({
            requestId: `fixture-play-${fixture.id}`,
            operation: "playAction",
            graphicInstanceId,
            params: {delta: 1}
        });
        await waitForResponseCount(++expectedResponses);
        assert.equal(responses.at(-1).value.currentStep, 0);
        if (fixture.expectedColor) {
            assert.equal(root.children[0].style.opacity, "1");
            assert.equal(root.children[0].style.transform, "translateY(0)");
        }

        if (fixture.id === "lower-third") {
            window.__casparOgraphDispatch({
                requestId: "fixture-play-lower-third-to-end",
                operation: "playAction",
                graphicInstanceId,
                params: {delta: 1}
            });
            await waitForResponseCount(++expectedResponses);
            assert.equal(responses.at(-1).value.currentStep, undefined);
            assert.equal(root.children[0].style.opacity, "0");

            window.__casparOgraphDispatch({
                requestId: "fixture-replay-lower-third",
                operation: "playAction",
                graphicInstanceId,
                params: {delta: 1}
            });
            await waitForResponseCount(++expectedResponses);
            assert.equal(responses.at(-1).value.currentStep, 0);
            assert.equal(root.children[0].style.opacity, "1");

            window.__casparOgraphDispatch({
                requestId: "fixture-stop-lower-third",
                operation: "stopAction",
                graphicInstanceId,
                params: {}
            });
            await waitForResponseCount(++expectedResponses);
            assert.equal(responses.at(-1).value.currentStep, undefined);
            assert.equal(root.children[0].style.opacity, "0");

            window.__casparOgraphDispatch({
                requestId: "fixture-play-lower-third-after-stop",
                operation: "playAction",
                graphicInstanceId,
                params: {delta: 1}
            });
            await waitForResponseCount(++expectedResponses);
            assert.equal(responses.at(-1).value.currentStep, 0);
            assert.equal(root.children[0].style.opacity, "1");
        }

        window.__casparOgraphDispatch({
            requestId: `fixture-dispose-${fixture.id}`,
            operation: "dispose",
            graphicInstanceId,
            params: {}
        });
        await waitForResponseCount(++expectedResponses);
        assert.equal(root.children.length, 0);
    }
    assert.equal(responses.length, 418);
    assert.ok(responses.every(response => response.ok));
} finally {
    await Promise.all([
        rm(hostModulePath, {force: true}),
        rm(graphicModulePath, {force: true})
    ]);
}
