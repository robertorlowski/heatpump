import {Component} from 'preact';
import './style.css';
import '../../api/api'
import { HpRequests } from '../../api/api';
import { TCO, THP, TPV } from '../../api/type';
import swith_on from '../../assets/swith_on.svg'
import swith_off from '../../assets/swith_off.svg'


interface IState {
    data?: TCO;
}

export default class HP extends Component<{}, IState> {

	componentWillMount() {
		HpRequests.getCoData().then(resp => {
			this.setState({ data: resp })
		   }
		).catch(err =>{
			console.log(err);
		   }
		);
	};

	render({}, {}) {
		if (!this.state.data) {	
			return 'Loading...';
		}

		let _pv: TPV = this.state.data.PV;
		let _hp: THP = this.state.data.HP;
		let _data: TCO = this.state.data;

		return (
			<div class="settings">
				<h2>CWU / CO</h2>
				<section>
					<div  class="resource">
						<div class="heet">
							<div class="heat head">
								<div>
									<span class ="">{_data.time}</span>
								</div>
								<div>
									<span class ="" >Tryb pracy </span>
									<span class ="field">{
											_data.work_mode == "M" ? "ręczny" :
												_data.work_mode == "A" ? "automatyczny" :
													_data.work_mode == "PV" ? "automatyczny z PV" :
														_data.work_mode == "CWU" ? " CWU " : 
															_data.work_mode == "OFF" ? " OFF " : " --- "
										}
									</span>
								</div>
							</div>
							<div class="heat body">
							{/* <table class="heat table" style="width: 30	%;">
									<thead></thead>
									<tbody>
										<tr class="left">
											<td class ="label" style="min-width: 80px;">CO:</td>
											<td>
												{_hp.CO == undefined ? "--" : 
													<img title="CO" src={ _hp.CO ? swith_on : swith_off}/>
												}
											</td>
										</tr>
										<tr class="left">
											<td class ="label" style="min-width: 80px;">CWU:</td>
											<td>
												{_hp.CWU == undefined ? "--" : 
													<img title="CWU" src={ _hp.CWU ? swith_on : swith_off}/>
												}
											</td>
										</tr> 
									</tbody>
									<tfoot></tfoot>													
								</table> */}
								<table class="heat table" style="width: 40%; font-size: 32px; justify-content: center;">
									<thead></thead>
									<tbody>
										<tr>
											<td class ="label">CO:</td>
											<td className= {_data.co_pomp ? `field incorrect` : `field correct`}>
													{_hp.Ttarget ? _hp.Ttarget : "---"}
											</td>
											
										</tr>
										<tr>
											<td class ="label">CWU:</td>
											<td className= { _data.cwu_pomp ? `field incorrect` : `field correct`}>
													{_hp.Tcwu ? _hp.Tcwu : "---"}
											</td>
										</tr>
									</tbody>
									<tfoot></tfoot>													
								</table>
								<table class="heat table" style="width: 30%;">
									<thead></thead>
									<tbody>
										<tr class="left">
											<td class ="label">CO pump:</td>
											<td>
												{_data.co_pomp == undefined ? "---" : 		
													<img title="CO pump" src={ _data.co_pomp ? swith_on : swith_off}/>
												}
											</td>
										</tr>
										<tr>
											<td class ="label">CWU pump:</td>
											<td>
												{_hp.CWUS == undefined ? "---" : 
													<img title="CWU circle state" src={ _hp.CWUS ? swith_on : swith_off}/>
												}
											</td>
										</tr>
									</tbody>
									<tfoot></tfoot>													
								</table>	
							</div>
							<div class="heat footer">
							<table>
								<thead></thead>
								<tbody>
									<tr>
										<td>
											PV:
											<span className= {_data.pv_power ? `field correct` : `field`} style="margin-left: 5px;">
												{_pv && _pv.total_power ? _pv.total_power : "---"} </span> W
										</td>
									</tr>
									</tbody>
									<tfoot></tfoot>													
								</table>
							</div>
						</div>
					</div>   
					<br/>
					<div  class="resource">
						<h3>Pompa ciepła</h3>
						<hr/>
						<div class="hp-data">
							<table>
								<thead></thead>
								<tbody>
									{/* <tr>
										<td class ="label">Temp. CO:</td>
										<td className= {(_hp.Ttarget > _hp.Tmin && _hp.Ttarget < _hp.Tmax)
											? `field correct` : `field incorrect`}>{_hp.Ttarget}</td>
									</tr> */}
									<tr>
										<td class ="label">Temp. CO min:</td>
										<td class="field">{_hp.Tmin ?_hp.Tmin : "---"}</td>
									</tr>
									{/* <tr>
										<td class ="label">Temp. CWU min:</td>
										<td class="field">{_hp.Tcwu_min ?_hp.Tcwu_min : "---"}</td>
									</tr> */}
									<tr>
										<td class ="label">Temp. BE</td>
										<td className= {(_hp.Tbe < 0) ? `field incorrect` : `field `}>{_hp.Tbe ?_hp.Tbe : "---"}</td>
									</tr>
									<tr>
										<td class ="label">Temp. cold out:</td>
										<td className= {(_hp.Tco < 0) ? `field incorrect` : `field `}>{_hp.Tco ?_hp.Tco : "---"}</td>									
									</tr>	
									<tr>
										<td class ="label">Temp. comp.:</td>
										<td class="field">{_hp.Tsump ?_hp.Tsump : "---"}</td>
									</tr>
									<tr>
										<td class ="label">EEV dt:</td>
										<td class="field">{_hp.EEV_dt ?_hp.EEV_dt : "---"}</td>
									</tr>
									<tr>
										<td class ="label">EEV position:</td>
										<td class="field">{_hp.EEV_pos ?_hp.EEV_pos : "---"}</td>
									</tr>	
								</tbody>
								<tfoot></tfoot>													
							</table>
							<table>
								<thead></thead>
								<tbody>
									{/* <tr>
										<td class ="label">Temp. CWU:</td>
										<td className= {(_hp.Tcwu > _hp.Tcwu_min && _hp.Tcwu < _hp.Tcwu_max)
											? `field correct` : `field incorrect`}>{_hp.Tcwu}</td>
									</tr> */}
									<tr>
										<td class ="label">Temp. CO max:</td>
										<td class="field">{_hp.Tmax ?_hp.Tmax : "---"}</td>
									</tr>
									{/* <tr>
										<td class ="label">Temp CWU max:</td>
										<td class="field">{_hp.Tcwu_max ?_hp.Tcwu_max : "---"}</td>
									</tr> */}
									<tr>
										<td class ="label">Temp. AE:</td>
										<td className= {(_hp.Tae < 0) ? `field incorrect` : `field `}>{_hp.Tae ?_hp.Tae : "---"}</td>
									</tr>
									<tr>
										<td class ="label">Temp. hot out:</td>
										<td class="field">{_hp.Tho ?_hp.Tho : "---"}</td>
									</tr>	
									<tr>
										<td class ="label">Watts:</td>
										<td class="field">{_hp.Watts ?_hp.Watts : "---"}</td>
									</tr>
									<tr>
										<td class ="label">EEV temp.:</td>
										<td class="field">{_hp.EEV ?_hp.EEV : "---"}</td>
									</tr>
									<tr>
										<td class ="label"></td>
										<td class="field"></td>
									</tr>	
								</tbody>
								<tfoot></tfoot>													
							</table>
						</div>
						<hr/>
						<div class="hp-data">
							<table>
								<thead></thead>
								<tbody>
									<tr>
										<td class ="label">Hot pump:</td>
										<td class="field"><img title="Hot circle state" src={ _hp.HCS ? swith_on : swith_off}/></td>
									</tr>
									<tr>
										<td class ="label">Cold pump:</td>
										<td class="field"><img title="Cold circle state" src={ _hp.CCS ? swith_on : swith_off}/></td>
									</tr>
								</tbody>
								<tfoot></tfoot>													
							</table>
							<table>
								<thead></thead>
								<tbody>
									<tr>
										<td class ="label">Compressor:</td>
										<td class="field"><img title="Compressor on/off" src={ _hp.HPS ? swith_on : swith_off}/></td>
									</tr>
									<tr>
										<td class ="label">Force:</td>
										<td class="field"><img title="Force (t. min = t. max)" src={ _hp.F ? swith_on : swith_off}/></td>
									</tr>
								</tbody>
								<tfoot></tfoot>													
							</table>
						</div>
					</div>

					<br/>
					<div class="resource">
						<h3>Instalacja fotowtaiczna</h3>
						<hr/>						
						<div>
							<ul>
								<li>Moc: <strong>{_pv && _pv.total_power ? _pv.total_power : "---"}  </strong> W</li>
								<li>Temperatura: <strong>{_pv && _pv.temperature ? _pv.temperature : "---"} </strong> C</li>	
								<li>Produkcja dziś: <strong>{_pv && _pv.total_prod_today ? _pv.total_prod_today : "---"}  </strong> W</li>
							</ul>
						</div>
						
					</div>
				</section>
			</div>
		)
	}

}


