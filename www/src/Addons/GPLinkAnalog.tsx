import { useTranslation } from 'react-i18next';
import { FormCheck, Row } from 'react-bootstrap';
import * as yup from 'yup';

import Section from '../Components/Section';
import FormControl from '../Components/FormControl';
import { AddonPropTypes } from '../Pages/AddonsConfigPage';

export const gplinkAnalogScheme = {
	GPLinkAnalogEnabled: yup.number().required().label('GPLink Analog Enabled'),
	gplinkAnalogLxPin: yup
		.number()
		.label('GPLink Left X Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
	gplinkAnalogLyPin: yup
		.number()
		.label('GPLink Left Y Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
	gplinkAnalogRxPin: yup
		.number()
		.label('GPLink Right X Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
	gplinkAnalogRyPin: yup
		.number()
		.label('GPLink Right Y Pin')
		.validateRangeWhenValue('GPLinkAnalogEnabled', -1, 63),
};

export const gplinkAnalogState = {
	GPLinkAnalogEnabled: 0,
	gplinkAnalogLxPin: -1,
	gplinkAnalogLyPin: -1,
	gplinkAnalogRxPin: -1,
	gplinkAnalogRyPin: -1,
};

const GPLinkAnalog = ({
	values,
	errors,
	handleChange,
	handleCheckbox,
}: AddonPropTypes) => {
	const { t } = useTranslation();
	return (
		<Section title={t('AddonsConfig:gplink-analog-header-text')}>
			<div id="GPLinkAnalogOptions" hidden={!values.GPLinkAnalogEnabled}>
				<div className="alert alert-info" role="alert">
					{t('AddonsConfig:gplink-analog-sub-header-text')}
				</div>
				<Row className="mb-3">
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-lx-pin-label')}
						name="gplinkAnalogLxPin"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogLxPin}
						error={errors.gplinkAnalogLxPin}
						isInvalid={Boolean(errors.gplinkAnalogLxPin)}
						onChange={handleChange}
						min={-1}
						max={63}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-ly-pin-label')}
						name="gplinkAnalogLyPin"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogLyPin}
						error={errors.gplinkAnalogLyPin}
						isInvalid={Boolean(errors.gplinkAnalogLyPin)}
						onChange={handleChange}
						min={-1}
						max={63}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-rx-pin-label')}
						name="gplinkAnalogRxPin"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogRxPin}
						error={errors.gplinkAnalogRxPin}
						isInvalid={Boolean(errors.gplinkAnalogRxPin)}
						onChange={handleChange}
						min={-1}
						max={63}
					/>
					<FormControl
						type="number"
						label={t('AddonsConfig:gplink-analog-ry-pin-label')}
						name="gplinkAnalogRyPin"
						className="form-control-sm"
						groupClassName="col-sm-3 mb-3"
						value={values.gplinkAnalogRyPin}
						error={errors.gplinkAnalogRyPin}
						isInvalid={Boolean(errors.gplinkAnalogRyPin)}
						onChange={handleChange}
						min={-1}
						max={63}
					/>
				</Row>
			</div>
			<FormCheck
				label={t('Common:switch-enabled')}
				type="switch"
				id="GPLinkAnalogButton"
				reverse
				isInvalid={false}
				checked={Boolean(values.GPLinkAnalogEnabled)}
				onChange={(e) => {
					handleCheckbox('GPLinkAnalogEnabled');
					handleChange(e);
				}}
			/>
		</Section>
	);
};

export default GPLinkAnalog;
