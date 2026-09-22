import { create } from 'zustand';
import WebApi, { baseUrl } from '../Services/WebApi';

type State = {
	boardDefinition: {
		minPin: number;
		maxPin: number;
		analogPins: Array<number>;
		availablePins: Array<number>;
		usedPins: {};
		pinNotes?: Record<string, string>;
	};
	loading: boolean;
	error: boolean;
};

type Actions = {
	getBoardDefinition: () => void;
};

const INITIAL_STATE: State = {
	boardDefinition: {
		minPin: -1,
		maxPin: -1,
		analogPins: [],
		availablePins: [],
		usedPins: {},
	},
	loading: false,
	error: false,
};

const useBoardDefinition = create<State & Actions>()((set) => ({
	...INITIAL_STATE,
	getBoardDefinition: async () => {
		set({ loading: true });

		try {
			const [definition] = await Promise.all([
				fetch(`${baseUrl}/api/getBoardDefinition`).then((res) => res.json()),
			]);

			// Pico wraps the definition in a "pico" object; the S3 port serves
			// the flat shape (no wrapper) — branch on the key, not the board.
			set({ boardDefinition: definition.pico ?? definition });
		} catch (error) {
			set({ error: true, loading: false });
		}
	},
}));

export default useBoardDefinition;
