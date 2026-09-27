import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Row, Table } from 'react-bootstrap';

import Section from '../Components/Section';
import FormControl from '../Components/FormControl';
import FormSelect from '../Components/FormSelect';
import WebApi from '../Services/WebApi';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

const TEST_FUNCTIONS = [
	{ value: 0, labelKey: 'AddonsConfig:gplink-test-fn-hold-low' },
	{ value: 1, labelKey: 'AddonsConfig:gplink-test-fn-hold-high' },
	{ value: 2, labelKey: 'AddonsConfig:gplink-test-fn-toggle' },
	{ value: 3, labelKey: 'AddonsConfig:gplink-test-fn-sweep-up' },
	{ value: 4, labelKey: 'AddonsConfig:gplink-test-fn-sweep-down' },
	{ value: 5, labelKey: 'AddonsConfig:gplink-test-fn-triangle' },
	{ value: 10, labelKey: 'AddonsConfig:gplink-test-fn-drive-low' },
	{ value: 11, labelKey: 'AddonsConfig:gplink-test-fn-drive-high' },
	{ value: 12, labelKey: 'AddonsConfig:gplink-test-fn-drive-cycle' },
	{ value: 13, labelKey: 'AddonsConfig:gplink-test-fn-drive-pwm' },
	{ value: 255, labelKey: 'AddonsConfig:gplink-test-fn-stop' },
];

const TEST_STATUSES = [
	'running',
	'done',
	'aborted',
	'bad-pin',
	'unsupported',
	'busy',
];

type LaunchedTest = {
	testId: number;
	pin: number;
	fn: number;
	p1: number;
	p2: number;
};

type TestResult = {
	testId: number;
	status: number;
	value: number;
	count: number;
};

const fnLabel = (fn: number, t: (k: string) => string) => {
	const entry = TEST_FUNCTIONS.find((f) => f.value === fn);
	return entry ? t(entry.labelKey) : `fn ${fn}`;
};

const GPLinkTester = ({ values }: AddonPropTypes) => {
	const { t } = useTranslation();

	const [pin, setPin] = useState(-1);
	const [fn, setFn] = useState(2);
	const [p1, setP1] = useState(1000);
	const [p2, setP2] = useState(5);
	const [nextId, setNextId] = useState(1);
	const [launched, setLaunched] = useState<LaunchedTest[]>([]);
	const [results, setResults] = useState<TestResult[]>([]);
	const [sendFailed, setSendFailed] = useState(false);

	useEffect(() => {
		let alive = true;
		let id: ReturnType<typeof setInterval> | null = null;
		const fetchResults = async () => {
			const data = await WebApi.getGPLinkTestResults();
			if (!alive) return;
			if (data && Array.isArray(data.results)) {
				setResults(data.results);
			}
		};
		const start = () => {
			fetchResults();
			if (id === null) id = setInterval(fetchResults, 1000);
		};
		const stop = () => {
			if (id !== null) {
				clearInterval(id);
				id = null;
			}
		};
		const onVisibility = () => {
			if (document.hidden) stop();
			else if (alive) start();
		};
		document.addEventListener('visibilitychange', onVisibility);
		if (!document.hidden) start();
		return () => {
			alive = false;
			stop();
			document.removeEventListener('visibilitychange', onVisibility);
		};
	}, []);

	const runTest = async (
		testId: number,
		testPin: number,
		testFn: number,
		testP1: number,
		testP2: number,
	) => {
		if (
			![testId, testPin, testFn, testP1, testP2].every((v) =>
				Number.isFinite(v),
			)
		) {
			setSendFailed(true);
			return;
		}
		const data = await WebApi.runGPLinkTest(
			testId,
			testPin,
			testFn,
			testP1,
			testP2,
		);
		if (data && data.sent) {
			setSendFailed(false);
			// Stops aren't launches: leave the history entry intact so the
			// row keeps its original pin/function while status updates.
			if (testFn !== 255) {
				setLaunched((prev) =>
					[
						{ testId, pin: testPin, fn: testFn, p1: testP1, p2: testP2 },
						...prev,
					].slice(0, 32),
				);
				setNextId((prev) => (prev % 254) + 1);
			}
		} else {
			setSendFailed(true);
		}
	};

	// Latest result per testId (server sends newest-first).
	const latestById = new Map<number, TestResult>();
	for (const r of results) {
		if (!latestById.has(r.testId)) latestById.set(r.testId, r);
	}

	return (
		<Section title={t('AddonsConfig:gplink-tester-header-text')}>
			<div
				id="GPLinkTesterOptions"
				hidden={!values.GPLinkEnabled && !values.GPLinkAnalogEnabled}
			>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:gplink-tester-sub-header-text')}
				</div>
				<Row className="mb-3">
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-tester-pin-label')}
						name="gplinkTestPin"
						className="form-control-sm"
						groupClassName="col-sm-2 mb-3"
						value={pin}
						onChange={(e) => setPin(parseInt(e.target.value, 10))}
						min={-1}
						max={63}
					/>
					<FormSelect
						label={t('AddonsConfig:gplink-tester-function-label')}
						name="gplinkTestFunction"
						className="form-select-sm"
						groupClassName="col-sm-3 mb-3"
						value={fn}
						onChange={(e) => setFn(parseInt(e.target.value, 10))}
					>
						{TEST_FUNCTIONS.map((o) => (
							<option key={`gplink-test-fn-${o.value}`} value={o.value}>
								{t(o.labelKey)}
							</option>
						))}
					</FormSelect>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-tester-param1-label')}
						name="gplinkTestParam1"
						className="form-control-sm"
						groupClassName="col-sm-2 mb-3"
						value={p1}
						onChange={(e) => setP1(parseInt(e.target.value, 10))}
						min={0}
						max={65535}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-tester-param2-label')}
						name="gplinkTestParam2"
						className="form-control-sm"
						groupClassName="col-sm-2 mb-3"
						value={p2}
						onChange={(e) => setP2(parseInt(e.target.value, 10))}
						min={0}
						max={65535}
					/>
					<div className="col-sm-3 mb-3 d-flex align-items-end gap-2">
						<Button size="sm" onClick={() => runTest(nextId, pin, fn, p1, p2)}>
							{t('AddonsConfig:gplink-tester-run-label')}
						</Button>
						<Button
							size="sm"
							variant="secondary"
							onClick={() => runTest(255, 0, 255, 0, 0)}
						>
							{t('AddonsConfig:gplink-tester-stop-all-label')}
						</Button>
					</div>
				</Row>
				<Row className="mb-3">
					<div className="col-sm-12">
						<p className="text-muted">
							<small>
								{t('AddonsConfig:gplink-tester-params-help-text')}
							</small>
						</p>
						{sendFailed && (
							<div className="alert alert-warning" role="alert">
								{t('AddonsConfig:gplink-tester-send-failed-text')}
							</div>
						)}
					</div>
				</Row>
				{results.length > 0 && (
					<Row className="mb-3">
						<div className="col-sm-12">
							<h6>{t('AddonsConfig:gplink-tester-results-header-text')}</h6>
							<Table striped bordered hover size="sm">
								<thead>
									<tr>
										<th>{t('AddonsConfig:gplink-tester-col-test')}</th>
										<th>{t('AddonsConfig:gplink-tester-col-pin')}</th>
										<th>{t('AddonsConfig:gplink-tester-col-function')}</th>
										<th>{t('AddonsConfig:gplink-tester-col-status')}</th>
										<th>{t('AddonsConfig:gplink-tester-col-value')}</th>
										<th>{t('AddonsConfig:gplink-tester-col-count')}</th>
										<th>{t('AddonsConfig:gplink-tester-col-action')}</th>
									</tr>
								</thead>
								<tbody>
									{results.map((r, i) => {
										const launch = launched.find(
											(l) => l.testId === r.testId,
										);
										const active = r.status === 0;
										return (
											<tr key={`gplink-test-result-${r.testId}-${i}`}>
												<td>{r.testId}</td>
												<td>{launch ? launch.pin : '—'}</td>
											<td>
												{launch
													? fnLabel(launch.fn, t)
													: '—'}
											</td>
											<td>
												{r.status >= 0 &&
												r.status < TEST_STATUSES.length
													? t(
															`AddonsConfig:gplink-test-status-${TEST_STATUSES[r.status]}`,
														)
													: r.status}
											</td>
												<td>{r.value}</td>
												<td>{r.count}</td>
												<td>
													{active && (
														<Button
															size="sm"
															variant="secondary"
															onClick={() =>
																runTest(r.testId, 0, 255, 0, 0)
															}
														>
															{t('AddonsConfig:gplink-tester-stop-label')}
														</Button>
													)}
												</td>
											</tr>
										);
									})}
								</tbody>
							</Table>
						</div>
					</Row>
				)}
			</div>
		</Section>
	);
};

export default GPLinkTester;
